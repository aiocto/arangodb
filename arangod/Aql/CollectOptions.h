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

#include <string>
#include <string_view>

namespace arangodb {
namespace velocypack {
class Builder;
class Slice;
}  // namespace velocypack
namespace aql {
struct Variable;

/// @brief CollectOptions
struct CollectOptions final {
  /// @brief selected aggregation method
  enum class CollectMethod { kUndefined, kHash, kSorted, kDistinct, kCount };

  /// @brief constructor, using default values
  CollectOptions() noexcept;

  explicit CollectOptions(velocypack::Slice);

  CollectOptions(CollectOptions const& other) = default;

  CollectOptions& operator=(CollectOptions const& other) = default;

  /// @brief whether or not the method has been fixed
  bool isFixed() const noexcept;

  /// @brief set method and fix it. note: some cluster optimizer rule
  /// adjusts the methods after it has been initially fixed.
  void fixMethod(CollectMethod m) noexcept;

  /// @brief whether or not the method can be used
  bool canUseMethod(CollectMethod m) const noexcept;

  /// @brief whether or not the method should be used
  bool shouldUseMethod(CollectMethod m) const noexcept;

  /// @brief convert the options to VelocyPack
  void toVelocyPack(velocypack::Builder&) const;

  /// @brief get the aggregation method from a string
  static CollectMethod methodFromString(std::string_view) noexcept;

  /// @brief stringify the aggregation method
  static std::string_view methodToString(CollectOptions::CollectMethod method);

  /// @brief type of COLLECT, e.g. sorted, hash, distinct, count...
  CollectMethod method;
  /// @brief if true, then the CollectMethod must not be changed after
  /// being set. if false, the CollectMethod can still change later.
  bool fixed;
};

struct GroupVarInfo final {
  Variable const* outVar;
  Variable const* inVar;
};

struct AggregateVarInfo final {
  Variable const* outVar;
  Variable const* inVar;
  std::string type;
};

}  // namespace aql
}  // namespace arangodb
