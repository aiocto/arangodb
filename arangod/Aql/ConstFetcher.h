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

#pragma once

#include "Aql/AqlItemBlockInputRange.h"
#include "Aql/ExecutionState.h"
#include "Aql/InputAqlItemRow.h"
#include "Aql/SkipResult.h"

#include <memory>
#include <span>

namespace arangodb::aql {

class AqlCallStack;
class AqlItemBlock;
template<BlockPassthrough>
class DependencyProxy;
class ShadowAqlItemRow;

/**
 * @brief Interface for all AqlExecutors that do only need one
 *        row at a time in order to make progress.
 */
class ConstFetcher {
  using DependencyProxy = aql::DependencyProxy<BlockPassthrough::Enable>;

 public:
  using DataRange = AqlItemBlockInputRange;
  explicit ConstFetcher(DependencyProxy& executionBlock);
  TEST_VIRTUAL ~ConstFetcher() = default;

 protected:
  // only for testing! Does not initialize _dependencyProxy!
  ConstFetcher();

 public:
  /**
   * @brief Execute the given call stack
   *
   * @param stack Call stack, on top of stack there is current subquery, bottom
   * is the main query.
   * @return std::tuple<ExecutionState, size_t, DataRange>
   *   ExecutionState => DONE, all queries are done, there will be no more
   *   ExecutionState => HASMORE, there are more results for queries, might be
   * on other subqueries ExecutionState => WAITING, we need to do I/O to solve
   * the request, save local state and return WAITING to caller immediately
   *
   *   size_t => Amount of documents skipped
   *   DataRange => Resulting data
   */
  auto execute(AqlCallStack const& stack)
      -> std::tuple<ExecutionState, SkipResult, DataRange>;

  void injectBlock(SharedAqlItemBlockPtr block, SkipResult skipped);

  void setDistributeId(std::string const&);

 private:
  /**
   * @brief Input block currently in use. Used for memory management by the
   *        ConstFetcher. May be moved if the Fetcher implementations
   *        are moved into separate classes.
   */
  SharedAqlItemBlockPtr _currentBlock;

  /**
   * @brief The amount of documents skipped in outer subqueries.
   *
   */
  SkipResult _skipped;

  SharedAqlItemBlockPtr _blockForPassThrough;

  /**
   * @brief Index of the row to be returned next. This is valid
   *        iff _currentBlock != nullptr and it's smaller or equal than
   *        _currentBlock->size(). May be moved if the Fetcher implementations
   *        are moved into separate classes.
   */
  size_t _rowIndex;

 private:
  auto indexIsValid() const noexcept -> bool;
  auto numRowsLeft() const noexcept -> size_t;
  auto canUseFullBlock(
      std::span<std::pair<size_t, size_t> const> ranges) const noexcept -> bool;
};

}  // namespace arangodb::aql
