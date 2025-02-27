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
/// @author Markus Pfeiffer
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/ModificationExecutorFlags.h"
#include "Aql/RegisterInfos.h"
#include "Utils/OperationOptions.h"
#include "VocBase/LogicalCollection.h"

#include <velocypack/Slice.h>

namespace arangodb::aql {
struct Collection;
class ExecutionEngine;
class QueryContext;

struct ModificationExecutorInfos {
  ModificationExecutorInfos(
      ExecutionEngine* engine, RegisterId input1RegisterId,
      RegisterId input2RegisterId, RegisterId input3RegisterId,
      RegisterId outputNewRegisterId, RegisterId outputOldRegisterId,
      RegisterId outputRegisterId, arangodb::aql::QueryContext& query,
      OperationOptions options, aql::Collection const* aqlCollection,
      size_t batchSize, ProducesResults producesResults,
      ConsultAqlWriteFilter consultAqlWriteFilter, IgnoreErrors ignoreErrors,
      DoCount doCount, IsReplace isReplace,
      IgnoreDocumentNotFound ignoreDocumentNotFound);

  ModificationExecutorInfos() = delete;
  ModificationExecutorInfos(ModificationExecutorInfos&&) = default;
  ModificationExecutorInfos(ModificationExecutorInfos const&) = delete;
  ~ModificationExecutorInfos() = default;

  ExecutionEngine* engine() const { return _engine; }

  /// @brief the variable produced by Return
  arangodb::aql::ExecutionEngine* _engine;
  arangodb::aql::QueryContext& _query;
  OperationOptions _options;
  aql::Collection const* _aqlCollection;
  size_t _batchSize;
  ProducesResults _producesResults;
  ConsultAqlWriteFilter _consultAqlWriteFilter;
  IgnoreErrors _ignoreErrors;
  DoCount _doCount;  // count statisitics
  // bool _returnInheritedResults;
  IsReplace _isReplace;                            // needed for upsert
  IgnoreDocumentNotFound _ignoreDocumentNotFound;  // needed for update replace

  // insert (singleinput) - upsert (inDoc) - update replace (inDoc)
  RegisterId _input1RegisterId;
  // upsert (insertVar) -- update replace (keyVar)
  RegisterId _input2RegisterId;
  // upsert (updateVar)
  RegisterId _input3RegisterId;

  RegisterId _outputNewRegisterId;
  RegisterId _outputOldRegisterId;
  RegisterId _outputRegisterId;  // single remote
};

}  // namespace arangodb::aql
