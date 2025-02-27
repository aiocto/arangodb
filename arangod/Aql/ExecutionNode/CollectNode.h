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

#include "Aql/CollectOptions.h"
#include "Aql/ExecutionNode/ExecutionNode.h"
#include "Aql/ExecutionNodeId.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace arangodb {
namespace velocypack {
class Slice;
}
namespace aql {
class ExecutionBlock;
class ExecutionPlan;
struct Aggregator;

/// @brief class CollectNode
class CollectNode : public ExecutionNode {
  friend class ExecutionNode;
  friend class ExecutionBlock;

 public:
  CollectNode(
      ExecutionPlan* plan, ExecutionNodeId id, CollectOptions const& options,
      std::vector<GroupVarInfo> const& groupVariables,
      std::vector<AggregateVarInfo> const& aggregateVariables,
      Variable const* expressionVariable, Variable const* outVariable,
      std::vector<std::pair<Variable const*, std::string>> const& keepVariables,
      std::unordered_map<VariableId, std::string const> const& variableMap);

  CollectNode(
      ExecutionPlan*, arangodb::velocypack::Slice base,
      Variable const* expressionVariable, Variable const* outVariable,
      std::vector<std::pair<Variable const*, std::string>> const& keepVariables,
      std::unordered_map<VariableId, std::string const> const& variableMap,
      std::vector<GroupVarInfo> const& collectVariables,
      std::vector<AggregateVarInfo> const& aggregateVariables);

  ~CollectNode() override;

  /// @brief return the type of the node
  NodeType getType() const override final;

  /// @brief return the amount of bytes used
  size_t getMemoryUsedBytes() const override final;

  /// @brief whether or not the collect type is fixed
  bool isFixedMethod() const noexcept;

  /// @brief return the aggregation method
  CollectOptions::CollectMethod aggregationMethod() const noexcept;

  /// @brief set the aggregation method
  void aggregationMethod(CollectOptions::CollectMethod method) noexcept;

  /// @brief getOptions
  CollectOptions& getOptions();

  /// @brief calculate the expression register
  void calcExpressionRegister(RegisterId& expressionRegister,
                              RegIdSet& writeableOutputRegisters) const;

  /// @brief calculate the collect register
  void calcCollectRegister(RegisterId& collectRegister,
                           RegIdSet& writeableOutputRegisters) const;

  /// @brief calculate the group registers
  void calcGroupRegisters(
      std::vector<std::pair<RegisterId, RegisterId>>& groupRegisters,
      RegIdSet& readableInputRegisters,
      RegIdSet& writeableOutputRegisters) const;

  /// @brief calculate the aggregate registers
  void calcAggregateRegisters(
      std::vector<std::pair<RegisterId, RegisterId>>& aggregateRegisters,
      RegIdSet& readableInputRegisters,
      RegIdSet& writeableOutputRegisters) const;

  void calcAggregateTypes(
      std::vector<std::unique_ptr<Aggregator>>& aggregateTypes) const;

  std::vector<std::pair<std::string, RegisterId>> calcInputVariableNames()
      const;

  /// @brief creates corresponding ExecutionBlock
  std::unique_ptr<ExecutionBlock> createBlock(
      ExecutionEngine& engine) const override;

  /// @brief clone ExecutionNode recursively
  ExecutionNode* clone(ExecutionPlan* plan,
                       bool withDependencies) const override final;

  /// @brief estimateCost
  CostEstimate estimateCost() const override final;

  AsyncPrefetchEligibility canUseAsyncPrefetching()
      const noexcept override final;

  /// @brief whether or not the node has an outVariable (i.e. INTO ...)
  bool hasOutVariable() const;

  /// @brief return the out variable
  Variable const* outVariable() const;

  /// @brief clear the out variable
  void clearOutVariable();

  /// @brief clear all keep variables
  void clearKeepVariables();

  void setAggregateVariables(
      std::vector<AggregateVarInfo>&& aggregateVariables);

  /// @brief clear one of the aggregates
  void clearAggregates(std::function<bool(AggregateVarInfo const&)> cb);

  /// @brief whether or not the node has an expression variable (i.e. INTO ...
  /// = expr)
  bool hasExpressionVariable() const noexcept;

  /// @brief set the expression variable
  void expressionVariable(Variable const* variable);

  /// @brief return whether or not the collect has keep variables
  bool hasKeepVariables() const noexcept;

  /// @brief return the keep variables
  std::vector<std::pair<Variable const*, std::string>> const& keepVariables()
      const;

  /// @brief restrict the KEEP variables (which may also be the auto-collected
  /// variables of an unrestricted `INTO var`) to the passed `variables`.
  void restrictKeepVariables(
      containers::HashSet<Variable const*> const& variables);

  /// @brief return the variable map
  std::unordered_map<VariableId, std::string const> const& variableMap() const;

  /// @brief get all group variables (out, in)
  std::vector<GroupVarInfo> const& groupVariables() const;

  /// @brief set all group variables (out, in)
  void groupVariables(std::vector<GroupVarInfo> vars);

  /// @brief get all aggregate variables (out, in)
  std::vector<AggregateVarInfo> const& aggregateVariables() const;

  /// @brief get all aggregate variables (out, in)
  std::vector<AggregateVarInfo>& aggregateVariables();

  void replaceVariables(std::unordered_map<VariableId, Variable const*> const&
                            replacements) override;

  /// @brief getVariablesUsedHere, modifying the set in-place
  void getVariablesUsedHere(VarSet& vars) const override final;

  /// @brief getVariablesSetHere
  std::vector<Variable const*> getVariablesSetHere() const override final;

  static void calculateAccessibleUserVariables(
      ExecutionNode const& node,
      std::vector<std::pair<Variable const*, std::string>>& userVariables);

 protected:
  /// @brief export to VelocyPack
  void doToVelocyPack(arangodb::velocypack::Builder&,
                      unsigned flags) const override final;

 private:
  /// @brief options for the aggregation
  CollectOptions _options;

  /// @brief input/output variables for the collection (out, in)
  std::vector<GroupVarInfo> _groupVariables;

  /// @brief input/output variables for the aggregation (out, in)
  std::vector<AggregateVarInfo> _aggregateVariables;

  /// @brief input expression variable (might be null)
  Variable const* _expressionVariable;

  /// @brief output variable to write to (might be null)
  Variable const* _outVariable;

  /// @brief list of variables to keep if INTO is used, the string value
  /// is the original variable name (which will resist any name change
  /// throughout query optimization)
  std::vector<std::pair<Variable const*, std::string>> _keepVariables;

  /// @brief map of all variable ids and names (needed to construct group data)
  std::unordered_map<VariableId, std::string const> _variableMap;
};

}  // namespace aql
}  // namespace arangodb
