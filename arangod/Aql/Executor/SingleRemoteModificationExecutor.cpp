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
/// @author Jan Christoph Uhde
////////////////////////////////////////////////////////////////////////////////

#include "SingleRemoteModificationExecutor.h"

#include "Aql/AqlValue.h"
#include "Aql/Collection.h"
#include "Aql/ExecutionBlockImpl.tpp"
#include "Aql/OutputAqlItemRow.h"
#include "Aql/RegisterPlan.h"
#include "Aql/SingleRowFetcher.h"
#include "Aql/QueryContext.h"
#include "Basics/StaticStrings.h"
#include "Cluster/ServerState.h"
#include "VocBase/LogicalCollection.h"

#include <velocypack/Builder.h>

#include <memory>

namespace arangodb::aql {

namespace {
std::unique_ptr<VPackBuilder> merge(VPackSlice document, std::string const& key,
                                    RevisionId revision) {
  auto builder = std::make_unique<VPackBuilder>();
  {
    VPackObjectBuilder guard(builder.get());
    TRI_SanitizeObject(document, *builder);
    VPackSlice keyInBody = document.get(StaticStrings::KeyString);

    if (keyInBody.isNone() || keyInBody.isNull() || keyInBody.isString() ||
        ((revision.isSet()) && (RevisionId::fromSlice(document) != revision))) {
      // We need to rewrite the document with the given revision and key:
      builder->add(StaticStrings::KeyString, VPackValue(key));
      if (revision.isSet()) {
        builder->add(StaticStrings::RevString, VPackValue(revision.toString()));
      }
    }
  }
  return builder;
}
}  // namespace

SingleRemoteModificationInfos::SingleRemoteModificationInfos(
    ExecutionEngine* engine, RegisterId inputRegister,
    RegisterId outputNewRegisterId, RegisterId outputOldRegisterId,
    RegisterId outputRegisterId, arangodb::aql::QueryContext& query,
    OperationOptions options, aql::Collection const* aqlCollection,
    ConsultAqlWriteFilter consultAqlWriteFilter, IgnoreErrors ignoreErrors,
    IgnoreDocumentNotFound ignoreDocumentNotFound, std::string key,
    bool hasParent, bool replaceIndex)
    : ModificationExecutorInfos(
          engine, inputRegister, RegisterPlan::MaxRegisterId,
          RegisterPlan::MaxRegisterId, outputNewRegisterId, outputOldRegisterId,
          outputRegisterId, query, std::move(options), aqlCollection,
          /*batchSize*/ 1, ProducesResults(false), consultAqlWriteFilter,
          ignoreErrors, DoCount(true), IsReplace(false),
          ignoreDocumentNotFound),
      _key(std::move(key)),
      _hasParent(hasParent),
      _replaceIndex(replaceIndex) {}

template<typename Modifier>
SingleRemoteModificationExecutor<Modifier>::SingleRemoteModificationExecutor(
    Fetcher& fetcher, Infos& info)
    : _trx(info._query.newTrxContext()),
      _info(info),
      _upstreamState(ExecutionState::HASMORE) {
  TRI_ASSERT(arangodb::ServerState::instance()->isCoordinator());
};

template<typename Modifier>
[[nodiscard]] auto SingleRemoteModificationExecutor<Modifier>::produceRows(
    AqlItemBlockInputRange& input, OutputAqlItemRow& output)
    -> std::tuple<ExecutorState,
                  typename SingleRemoteModificationExecutor<Modifier>::Stats,
                  AqlCall> {
  auto stats = Stats{};

  if (input.hasDataRow()) {
    auto [state, row] = input.nextDataRow();
    auto result = doSingleRemoteModificationOperation(row, stats);
    if (result.ok()) {
      doSingleRemoteModificationOutput(row, output, result);
    }
  }

  return {input.upstreamState(), stats, AqlCall{}};
}

template<typename Modifier>
[[nodiscard]] auto SingleRemoteModificationExecutor<Modifier>::skipRowsRange(
    AqlItemBlockInputRange& input, AqlCall& call)
    -> std::tuple<ExecutorState,
                  typename SingleRemoteModificationExecutor<Modifier>::Stats,
                  size_t, AqlCall> {
  auto stats = Stats{};

  if (input.hasDataRow()) {
    auto [state, row] = input.nextDataRow();
    auto result = doSingleRemoteModificationOperation(row, stats);
    if (result.ok()) {
      call.didSkip(1);
      return {input.upstreamState(), stats, 1, AqlCall{}};
    }
  }
  return {input.upstreamState(), stats, 0, AqlCall{}};
}

template<typename Modifier>
auto SingleRemoteModificationExecutor<
    Modifier>::doSingleRemoteModificationOperation(InputAqlItemRow& input,
                                                   Stats& stats)
    -> OperationResult {
  _info._options.silent = false;
  _info._options.returnOld =
      _info._options.returnOld || _info._outputRegisterId.isValid();

  OperationResult result(Result(), _info._options);

  const bool isIndex = std::is_same<Modifier, IndexTag>::value;
  const bool isInsert = std::is_same<Modifier, Insert>::value;
  const bool isRemove = std::is_same<Modifier, Remove>::value;
  const bool isUpdate = std::is_same<Modifier, Update>::value;
  const bool isReplace = std::is_same<Modifier, Replace>::value;

  uint64_t writesExecuted = 0;
  uint64_t writesIgnored = 0;

  if (_info._key.empty() &&
      _info._input1RegisterId.value() == RegisterId::maxRegisterId) {
    THROW_ARANGO_EXCEPTION_MESSAGE(TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND,
                                   "missing document reference");
  }

  VPackBuilder inBuilder;
  VPackSlice inSlice = VPackSlice::emptyObjectSlice();
  if (_info._input1RegisterId.isValid()) {  // IF NOT REMOVE OR SELECT
    AqlValue const& inDocument = input.getValue(_info._input1RegisterId);
    inBuilder.add(inDocument.slice());
    inSlice = inBuilder.slice();
  }

  std::unique_ptr<VPackBuilder> mergedBuilder = nullptr;
  if (!_info._key.empty()) {
    mergedBuilder = merge(inSlice, _info._key, RevisionId::none());
    inSlice = mergedBuilder->slice();
  }

  if (isIndex) {
    result =
        _trx.document(_info._aqlCollection->name(), inSlice, _info._options);
  } else if (isInsert) {
    if (_info._options.returnOld &&
        !_info._options.isOverwriteModeUpdateReplace()) {
      THROW_ARANGO_EXCEPTION_MESSAGE(
          TRI_ERROR_QUERY_VARIABLE_NAME_UNKNOWN,
          "OLD is only available when using INSERT with overwriteModes "
          "'update' or 'replace'");
    }
    result = _trx.insert(_info._aqlCollection->name(), inSlice, _info._options);
  } else if (isRemove) {
    result = _trx.remove(_info._aqlCollection->name(), inSlice, _info._options);
  } else if (isReplace) {
    if (_info._replaceIndex &&
        _info._input1RegisterId.value() == RegisterId::maxRegisterId) {
      // we have a FOR .. IN FILTER doc._key == ... REPLACE - no WITH.
      // in this case replace needs to behave as if it was UPDATE.
      result =
          _trx.update(_info._aqlCollection->name(), inSlice, _info._options);
    } else {
      result =
          _trx.replace(_info._aqlCollection->name(), inSlice, _info._options);
    }
  } else if (isUpdate) {
    result = _trx.update(_info._aqlCollection->name(), inSlice, _info._options);
  }

  if (!isIndex) {
    if (result.ok()) {
      writesExecuted++;
    } else {
      writesIgnored++;
    }
  }

  // check operation result
  if (!result.ok()) {
    if (result.is(TRI_ERROR_ARANGO_DOCUMENT_NOT_FOUND) &&
        (isIndex || (isUpdate && _info._replaceIndex) ||
         (isRemove && _info._replaceIndex) ||
         (isReplace && _info._replaceIndex))) {
      // document not there is not an error in this situation.
      // FOR ... FILTER ... REMOVE wouldn't invoke REMOVE in first place, so
      // don't throw an excetpion.
      return result;
    } else if (!_info._ignoreErrors) {  // TODO remove if
      THROW_ARANGO_EXCEPTION_MESSAGE(result.errorNumber(),
                                     result.errorMessage());
    }

    if (isIndex) {
      return result;
    }
  }

  stats.incrWritesExecuted(writesExecuted);
  stats.incrWritesIgnored(writesIgnored);
  // the increment of index is not correct when the executor doesn't apply the
  // single document optimization rule, but we leave the index calculation
  // incorrect here too to maintain compatibility with the execution without the
  // rule until fixed
  if (isIndex) {
    stats.incrScannedIndex();
  }
  return result;
}

template<typename Modifier>
auto SingleRemoteModificationExecutor<
    Modifier>::doSingleRemoteModificationOutput(InputAqlItemRow& input,
                                                OutputAqlItemRow& output,
                                                OperationResult& result)
    -> void {
  OperationOptions& options = _info._options;

  if (!(_info._outputRegisterId.isValid() ||
        _info._outputOldRegisterId.isValid() ||
        _info._outputNewRegisterId.isValid())) {
    if (_info._hasParent) {
      output.copyRow(input);
    }
    return;  //  _info._hasParent;
  }

  // Fill itemblock
  // create block that can hold a result with one entry and a number of
  // variables corresponding to the amount of out variables

  // only copy 1st row of registers inherited from previous frame(s)
  TRI_ASSERT(result.ok());
  VPackSlice outDocument = VPackSlice::nullSlice();
  if (result.buffer) {
    outDocument = result.slice().resolveExternal();
  }

  const bool isIndex = std::is_same<Modifier, IndexTag>::value;

  VPackSlice oldDocument = VPackSlice::nullSlice();
  VPackSlice newDocument = VPackSlice::nullSlice();
  if (!isIndex && outDocument.isObject()) {
    if (_info._outputNewRegisterId.isValid() &&
        outDocument.hasKey(StaticStrings::New)) {
      newDocument = outDocument.get(StaticStrings::New);
    }
    if (outDocument.hasKey(StaticStrings::Old)) {
      outDocument = outDocument.get(StaticStrings::Old);
      if (_info._outputOldRegisterId.isValid()) {
        oldDocument = outDocument;
      }
    }
  }

  TRI_ASSERT(_info._outputRegisterId.isValid() ||
             _info._outputOldRegisterId.isValid() ||
             _info._outputNewRegisterId.isValid());

  // place documents as in the out variable slots of the result
  if (_info._outputRegisterId.isValid()) {
    AqlValue value(outDocument);
    AqlValueGuard guard(value, true);
    output.moveValueInto(_info._outputRegisterId, input, &guard);
  }

  if (_info._outputOldRegisterId.isValid()) {
    TRI_ASSERT(options.returnOld);
    AqlValue value(oldDocument);
    AqlValueGuard guard(value, true);
    output.moveValueInto(_info._outputOldRegisterId, input, &guard);
  }

  if (_info._outputNewRegisterId.isValid()) {
    TRI_ASSERT(options.returnNew);
    AqlValue value(newDocument);
    AqlValueGuard guard(value, true);
    output.moveValueInto(_info._outputNewRegisterId, input, &guard);
  }

  TRI_IF_FAILURE("SingleRemoteModificationOperationBlock::moreDocuments") {
    THROW_ARANGO_EXCEPTION(TRI_ERROR_DEBUG);
  }
}

template struct SingleRemoteModificationExecutor<IndexTag>;
template struct SingleRemoteModificationExecutor<Insert>;
template struct SingleRemoteModificationExecutor<Remove>;
template struct SingleRemoteModificationExecutor<Replace>;
template struct SingleRemoteModificationExecutor<Update>;
template struct SingleRemoteModificationExecutor<Upsert>;

template class ExecutionBlockImpl<SingleRemoteModificationExecutor<IndexTag>>;
template class ExecutionBlockImpl<SingleRemoteModificationExecutor<Insert>>;
template class ExecutionBlockImpl<SingleRemoteModificationExecutor<Remove>>;
template class ExecutionBlockImpl<SingleRemoteModificationExecutor<Replace>>;
template class ExecutionBlockImpl<SingleRemoteModificationExecutor<Update>>;
template class ExecutionBlockImpl<SingleRemoteModificationExecutor<Upsert>>;

}  // namespace arangodb::aql
