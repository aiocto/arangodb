////////////////////////////////////////////////////////////////////////////////
/// DISCLAIMER
///
/// Copyright 2014-2024 ArangoDB GmbH, Cologne, Germany
/// Copyright 2004-2014 triAGENS GmbH, Cologne, Germany
///
/// Licensed under the Business Source License 1.1 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     https://github.com/arangodb/arangodb/blob/devel/LICENSE
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Daniel Larkin
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#include "ConstrainedSortExecutor.h"

#include "Aql/AqlItemBlockManager.h"
#include "Aql/ExecutionBlockImpl.tpp"
#include "Aql/InputAqlItemRow.h"
#include "Aql/OutputAqlItemRow.h"
#include "Aql/SingleRowFetcher.h"
#include "Aql/Executor/SortExecutor.h"
#include "Aql/SortRegister.h"
#include "Aql/Stats.h"
#include "Basics/Exceptions.h"
#include "Basics/ResourceUsage.h"

#include <algorithm>

namespace arangodb::aql {

namespace {

void eraseRow(SharedAqlItemBlockPtr& block, size_t row) {
  auto const nrRegs = block->numRegisters();
  for (RegisterId::value_t i = 0; i < nrRegs; i++) {
    block->destroyValue(row, i);
  }
}

}  // namespace

class ConstrainedLessThan {
 public:
  ConstrainedLessThan(velocypack::Options const* options,
                      std::vector<SortRegister> const& sortRegisters) noexcept
      : _vpackOptions(options),
        _heapBuffer(nullptr),
        _sortRegisters(sortRegisters) {}

  void setBuffer(AqlItemBlock* heap) { _heapBuffer = heap; }

  bool operator()(size_t const& a, size_t const& b) const {
    TRI_ASSERT(_heapBuffer);

    for (auto const& sortReg : _sortRegisters) {
      auto const& lhs = _heapBuffer->getValueReference(a, sortReg.reg);
      auto const& rhs = _heapBuffer->getValueReference(b, sortReg.reg);

      int const cmp = AqlValue::Compare(_vpackOptions, lhs, rhs, true);

      if (cmp < 0) {
        return sortReg.asc;
      } else if (cmp > 0) {
        return !sortReg.asc;
      }
    }

    return false;
  }

 private:
  velocypack::Options const* const _vpackOptions;
  AqlItemBlock* _heapBuffer;
  std::vector<SortRegister> const& _sortRegisters;
};  // ConstrainedLessThan

void ConstrainedSortExecutor::pushRow(InputAqlItemRow const& input,
                                      Stats& stats) {
  size_t dRow = _rowsPushed;

  if (dRow >= _infos.limit()) {
    // pop an entry first
    std::pop_heap(_rows.begin(), _rows.end(), *_cmpHeap);
    dRow = _rows.back();
    eraseRow(_heapBuffer, dRow);
    stats.incrFiltered();
  } else {
    _rows.emplace_back(dRow);  // add to heap vector
  }

  TRI_ASSERT(dRow < _infos.limit());
  TRI_IF_FAILURE("SortBlock::doSortingInner") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }

  _heapOutputRow.setBaseIndex(dRow);
  _heapOutputRow.copyRow(input);
  _heapOutputRow.advanceRow();

  ++_rowsPushed;

  // now restore heap condition
  std::push_heap(_rows.begin(), _rows.end(), *_cmpHeap);
}

bool ConstrainedSortExecutor::compareInput(size_t rowPos,
                                           InputAqlItemRow const& row) const {
  for (auto const& reg : _infos.sortRegisters()) {
    auto const& lhs = _heapBuffer->getValueReference(rowPos, reg.reg);
    auto const& rhs = row.getValue(reg.reg);

    int cmp = AqlValue::Compare(_infos.vpackOptions(), lhs, rhs, true);

    if (cmp < 0) {
      return reg.asc;
    } else if (cmp > 0) {
      return !reg.asc;
    }
  }
  return false;
}

namespace {

auto initRegsToKeep(RegisterCount size) -> RegIdFlatSetStack {
  auto regsToKeepStack = RegIdFlatSetStack{};
  auto& regsToKeep = regsToKeepStack.emplace_back();
  for (RegisterId::value_t i = 0; i < size; i++) {
    regsToKeep.emplace(i);
  }
  return regsToKeepStack;
}

}  // namespace

ConstrainedSortExecutor::ConstrainedSortExecutor(Fetcher& fetcher,
                                                 SortExecutorInfos& infos)
    : _infos(infos),
      _returnNext(0),
      _rowsPushed(0),
      _rowsRead(0),
      _skippedAfter(0),
      _heapBuffer(_infos.itemBlockManager().requestBlock(
          _infos.limit(), _infos.numberOfOutputRegisters())),
      _cmpHeap(std::make_unique<ConstrainedLessThan>(_infos.vpackOptions(),
                                                     _infos.sortRegisters())),
      _regsToKeep(initRegsToKeep(_infos.numberOfOutputRegisters())),
      _heapOutputRow{_heapBuffer, _outputRegister, _regsToKeep,
                     _infos.registersToClear()} {
  TRI_ASSERT(_infos.limit() > 0);

  {
    arangodb::ResourceUsageScope guard(_infos.getResourceMonitor(),
                                       memoryUsageForSort());

    _rows.reserve(infos.limit());

    // now we are responsible for memory tracking
    guard.steal();
  }

  _cmpHeap->setBuffer(_heapBuffer.get());
}

ConstrainedSortExecutor::~ConstrainedSortExecutor() {
  _infos.getResourceMonitor().decreaseMemoryUsage(memoryUsageForSort());
}

bool ConstrainedSortExecutor::doneProducing() const noexcept {
  // must not get strictly larger
  TRI_ASSERT(_returnNext <= _rows.size());
  return _returnNext >= _rows.size();
}

bool ConstrainedSortExecutor::doneSkipping() const noexcept {
  // must not get strictly larger
  TRI_ASSERT(_returnNext + _skippedAfter <= _rowsRead);
  return _returnNext + _skippedAfter >= _rowsRead;
}

ExecutorState ConstrainedSortExecutor::consumeInput(
    AqlItemBlockInputRange& inputRange, Stats& stats) {
  while (inputRange.hasDataRow()) {
    TRI_IF_FAILURE("SortBlock::doSorting") {
      THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
    }

    auto const& [state, input] =
        inputRange.nextDataRow(AqlItemBlockInputRange::HasDataRow{});
    // Otherwise we would have left the loop
    TRI_ASSERT(input.isInitialized());
    ++_rowsRead;
    if (_rowsPushed < _infos.limit() || !compareInput(_rows.front(), input)) {
      // Push this row into the heap
      pushRow(input, stats);
    } else {
      stats.incrFiltered();
    }
  }
  if (inputRange.upstreamState() == ExecutorState::DONE) {
    if (_returnNext == 0) {
      // Only once sort the rows again, s.t. the
      // contained list of elements is in the right ordering.
      std::sort(_rows.begin(), _rows.end(), *_cmpHeap);
    }
  }
  return inputRange.upstreamState();
}

auto ConstrainedSortExecutor::produceRows(AqlItemBlockInputRange& input,
                                          OutputAqlItemRow& output)
    -> std::tuple<ExecutorState, Stats, AqlCall> {
  FilterStats stats{};

  if (consumeInput(input, stats) == ExecutorState::HASMORE) {
    // Input could not be fully consumed, executor is more hungry!
    // Get more.
    // We need to fetch everything form upstream.
    // Unlimited, no offset call.
    return {ExecutorState::HASMORE, stats, AqlCall{}};
  };

  while (!output.isFull() && !doneProducing()) {
    // Now our heap is full and sorted, we just need to return it line by line
    TRI_ASSERT(_returnNext < _rows.size());
    auto heapRowPosition = _rows[_returnNext];
    ++_returnNext;
    InputAqlItemRow heapRow(_heapBuffer, heapRowPosition);
    TRI_ASSERT(heapRow.isInitialized());
    TRI_ASSERT(heapRowPosition < _rowsPushed);
    output.copyRow(heapRow);
    output.advanceRow();
  }
  if (doneProducing() && doneSkipping()) {
    return {ExecutorState::DONE, stats, AqlCall{}};
  }
  return {ExecutorState::HASMORE, stats, AqlCall{}};
}

auto ConstrainedSortExecutor::skipRowsRange(AqlItemBlockInputRange& inputRange,
                                            AqlCall& call)
    -> std::tuple<ExecutorState, Stats, size_t, AqlCall> {
  FilterStats stats{};

  if (consumeInput(inputRange, stats) == ExecutorState::HASMORE) {
    // Input could not be fully consumed, executor is more hungry!
    // Get more.
    // We need to fetch everything form upstream.
    // Unlimited, no offset call.
    return {ExecutorState::HASMORE, stats, 0, AqlCall{}};
  }

  while (!doneProducing()) {
    if (call.getOffset() > 0) {
      size_t available = _rows.size() - _returnNext;
      size_t toSkip = std::min(available, call.getOffset());
      _returnNext += toSkip;
      call.didSkip(toSkip);
    } else if (call.needSkipMore()) {
      // We are in fullcount case, simply skip all!
      // I think this is actually invalid, as it would case LIMIT
      // to underfetch.
      // However will work like this.
      size_t available = _rows.size() - _returnNext;
      call.didSkip(available);
      _returnNext = _rows.size();
    } else {
      // We still have something, but cannot continue to skip.
      return {ExecutorState::HASMORE, stats, call.getSkipCount(), AqlCall{}};
    }
  }

  while (call.needSkipMore() && !doneSkipping()) {
    auto rowsLeftToSkip = _rowsRead - (_rows.size() + _skippedAfter);
    // unlikely, but for backwards compatibility.
    if (call.getOffset() > 0) {
      auto skipNum = (std::min)(call.getOffset(), rowsLeftToSkip);
      call.didSkip(skipNum);
      _skippedAfter += skipNum;
    } else {
      // Fullcount
      call.didSkip(rowsLeftToSkip);
      _skippedAfter += rowsLeftToSkip;
      TRI_ASSERT(doneSkipping());
    }
  }

  auto state = doneSkipping() ? ExecutorState::DONE : ExecutorState::HASMORE;

  return {state, stats, call.getSkipCount(), AqlCall{}};
}

[[nodiscard]] auto ConstrainedSortExecutor::expectedNumberOfRows(
    AqlItemBlockInputRange const& input, AqlCall const& call) const noexcept
    -> size_t {
  size_t rowsPerBlock = _infos.limit();
  size_t subqueries = input.countShadowRows();
  if (subqueries == 0) {
    // we are a top level block, just pretend we run in the only subquery.
    subqueries = 1;
  }
  // we return rowsPerBlock for every subquery.
  size_t totalRows = subqueries * rowsPerBlock;
  // We can only have returnNext reach the total amount of Rows in a block
  // We have at least 1 block, hence totalRows needs to be higher
  TRI_ASSERT(_returnNext <= totalRows);
  // We have totalRows at most available.
  // adn we have _returnNext many of them already returned
  // from the first data-rows block.
  // In unlucky case we overestumate here, if we get called
  // while operating on any other data-row block then the first.
  if (input.countShadowRows() == 0) {
    return std::min(call.getLimit(), totalRows - _returnNext);
  }
  return totalRows - _returnNext;
}

size_t ConstrainedSortExecutor::memoryUsageForSort() const noexcept {
  return _infos.limit() * sizeof(decltype(_rows)::value_type);
}

template class ExecutionBlockImpl<ConstrainedSortExecutor>;

}  // namespace arangodb::aql
