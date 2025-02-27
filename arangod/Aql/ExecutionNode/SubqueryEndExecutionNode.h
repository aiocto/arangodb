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
/// @author Markus Pfeiffer
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/ExecutionNode/ExecutionNode.h"
#include "Aql/ExecutionNodeId.h"

namespace arangodb::aql {

class SubqueryEndNode : public ExecutionNode {
  friend class ExecutionNode;
  friend class ExecutionBlock;

 public:
  SubqueryEndNode(ExecutionPlan*, arangodb::velocypack::Slice const& base);

  SubqueryEndNode(ExecutionPlan* plan, ExecutionNodeId id,
                  Variable const* inVariable, Variable const* outVariable);

  CostEstimate estimateCost() const override final;

  NodeType getType() const override final { return SUBQUERY_END; }

  /// @brief return the amount of bytes used
  size_t getMemoryUsedBytes() const override final;

  Variable const* inVariable() const { return _inVariable; }

  Variable const* outVariable() const { return _outVariable; }

  std::unique_ptr<ExecutionBlock> createBlock(
      ExecutionEngine& engine) const override;

  ExecutionNode* clone(ExecutionPlan* plan,
                       bool withDependencies) const override final;

  bool isEqualTo(ExecutionNode const& other) const override final;

  void getVariablesUsedHere(VarSet& usedVars) const override final {
    if (_inVariable != nullptr) {
      usedVars.emplace(_inVariable);
    }
  }

  std::vector<Variable const*> getVariablesSetHere() const override final {
    return std::vector<Variable const*>{_outVariable};
  }

  void replaceOutVariable(Variable const* var);

  // We only override this to TRI_ASSERT(false), because
  // noone should ever ask this node whether it is a modification
  // node
  bool isModificationNode() const override;

 protected:
  void doToVelocyPack(arangodb::velocypack::Builder&,
                      unsigned flags) const override final;

 private:
  Variable const* _inVariable;
  Variable const* _outVariable;
};

}  // namespace arangodb::aql
