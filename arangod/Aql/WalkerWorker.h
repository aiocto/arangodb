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
/// @author Max Neunhoeffer
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include "Aql/types.h"
#include "Basics/debugging.h"
#include "Containers/HashSet.h"

namespace arangodb {
namespace aql {

enum class WalkerUniqueness : std::uint8_t { Unique, NonUnique };

/// @brief base interface to walk an execution plan recursively.
template<class T>
class WalkerWorkerBase {
 public:
  WalkerWorkerBase() = default;

  virtual ~WalkerWorkerBase() = default;

  /// @brief return true to abort walking, false otherwise
  virtual bool before(T*) {
    return false;  // true to abort the whole walking process
  }

  virtual void after(T*) {}

  /// @brief return true to enter subqueries, false otherwise
  virtual bool enterSubquery(T* /*super*/, T* /*sub*/) { return true; }

  virtual void leaveSubquery(T* /*super*/, T* /*sub*/) {}

  virtual bool done(T* /*en*/) { return false; }
};

/// @brief functionality to walk an execution plan recursively.
/// if template parameter `unique == true`, this will visit each node once, even
/// if multiple paths lead to the same node. no assertions are raised if
/// multiple paths lead to the same node
template<class T, WalkerUniqueness U>
class WalkerWorker : public WalkerWorkerBase<T> {
 public:
  virtual bool done([[maybe_unused]] T* en) override {
    if constexpr (U == WalkerUniqueness::Unique) {
      return !_done.emplace(en).second;
    }

    return false;
  }

  void reset() {
    if constexpr (U == WalkerUniqueness::Unique) {
      _done.clear();
      return;
    }
  }

 private:
  ::arangodb::containers::HashSet<T*> _done;
};

}  // namespace aql
}  // namespace arangodb
