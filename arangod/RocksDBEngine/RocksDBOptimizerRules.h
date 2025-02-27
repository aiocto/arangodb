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
/// @author Jan Steemann
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <memory>

namespace arangodb {
namespace aql {
class ExecutionPlan;
class Optimizer;
struct OptimizerRule;
class OptimizerRulesFeature;
}  // namespace aql

struct RocksDBOptimizerRules {
  static void registerResources(aql::OptimizerRulesFeature& feature);

  // simplify an EnumerationCollectionNode that fetches an entire document to a
  // projection of this document
  static void reduceExtractionToProjectionRule(
      aql::Optimizer* opt, std::unique_ptr<aql::ExecutionPlan> plan,
      aql::OptimizerRule const& rule);
  // remove SORT RAND() LIMIT 1 if appropriate
  static void removeSortRandRule(aql::Optimizer* opt,
                                 std::unique_ptr<aql::ExecutionPlan> plan,
                                 aql::OptimizerRule const& rule);
};

}  // namespace arangodb
