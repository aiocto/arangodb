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

#include "Aql/ExecutionNodeId.h"

namespace arangodb::aql {
struct Collection;
struct Variable;

class DataAccessingNode {
 public:
  virtual ~DataAccessingNode() = default;
  virtual Collection const* collection() const = 0;
  virtual bool isUsedAsSatellite() const = 0;
  virtual void useAsSatelliteOf(ExecutionNodeId) = 0;
  virtual Collection const* prototypeCollection() const = 0;
  virtual void setPrototype(Collection const*, Variable const*) = 0;
};

}  // namespace arangodb::aql
