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

#include "Aql/Scopes.h"
#include "Aql/Variable.h"
#include "Basics/Exceptions.h"
#include "Basics/debugging.h"

using namespace arangodb::aql;

/// @brief create the scope
Scope::Scope(ScopeType type) : _type(type) {}

/// @brief destroy the scope
Scope::~Scope() = default;

/// @brief return the name of a scope type
std::string Scope::typeName() const { return typeName(_type); }

/// @brief return the name of a scope type
std::string Scope::typeName(ScopeType type) {
  switch (type) {
    case AQL_SCOPE_MAIN:
      return "main";
    case AQL_SCOPE_SUBQUERY:
      return "subquery";
    case AQL_SCOPE_FOR:
      return "for";
    case AQL_SCOPE_COLLECT:
      return "collection";
  }
  TRI_ASSERT(false);
  return "unknown";
}

/// @brief adds a variable to the scope
void Scope::addVariable(Variable* variable) {
  // intentionally like this... must always overwrite the value
  // if the key already exists
  _variables[variable->name] = variable;
}

/// @brief checks if a variable exists in the scope
bool Scope::existsVariable(std::string_view name) const {
  return getVariable(name) != nullptr;
}

/// @brief returns a variable
Variable const* Scope::getVariable(std::string_view name) const {
  // TODO: use heterogenous lookups!
  std::string const varname(name);

  auto it = _variables.find(varname);

  if (it == _variables.end()) {
    return nullptr;
  }

  return (*it).second;
}

/// @brief return a variable, allowing usage of special pseudo vars such
/// as OLD and NEW
Variable const* Scope::getVariable(std::string_view name,
                                   bool allowSpecial) const {
  auto variable = getVariable(name);

  if (variable == nullptr && allowSpecial) {
    // variable does not exist
    // now try variable aliases OLD (= $OLD) and NEW (= $NEW)
    if (name == "OLD") {
      variable = getVariable(Variable::NAME_OLD);
    } else if (name == "NEW") {
      variable = getVariable(Variable::NAME_NEW);
    }
  }

  return variable;
}

/// @brief create the scopes
Scopes::Scopes() { _activeScopes.reserve(4); }

/// @brief destroy the scopes
Scopes::~Scopes() = default;

/// @brief return the type of the currently active scope
ScopeType Scopes::type() const {
  TRI_ASSERT(numActive() > 0);
  return _activeScopes.back()->type();
}

/// @brief whether or not the $CURRENT variable can be used at the caller's
/// current position
bool Scopes::canUseCurrentVariable() const noexcept {
  return !_currentVariables.empty();
}

/// @brief start a new scope
void Scopes::start(ScopeType type) {
  _activeScopes.emplace_back(std::make_unique<Scope>(type));
}

/// @brief end the current scope
void Scopes::endCurrent() {
  TRI_ASSERT(!_activeScopes.empty());
  _activeScopes.pop_back();
}

/// @brief end the current scope plus any FOR scopes it is nested in
void Scopes::endNested() {
  TRI_ASSERT(!_activeScopes.empty());

  while (!_activeScopes.empty()) {
    auto const& scope = _activeScopes.back();
    ScopeType type = scope->type();

    if (type == AQL_SCOPE_MAIN || type == AQL_SCOPE_SUBQUERY) {
      // the main scope and subquery scopes cannot be closed here
      break;
    }

    TRI_ASSERT(type == AQL_SCOPE_FOR || type == AQL_SCOPE_COLLECT);
    endCurrent();
  }
}

/// @brief adds a variable to the current scope
void Scopes::addVariable(Variable* variable) {
  TRI_ASSERT(!_activeScopes.empty());
  TRI_ASSERT(variable != nullptr);

  for (auto it = _activeScopes.rbegin(); it != _activeScopes.rend(); ++it) {
    auto const& scope = (*it);

    if (scope->existsVariable(variable->name)) {
      // duplicate variable name
      THROW_ARANGO_EXCEPTION_PARAMS(TRI_ERROR_QUERY_VARIABLE_REDECLARED,
                                    variable->name.c_str());
    }
  }

  // if this fails, there won't be a memleak
  _activeScopes.back()->addVariable(variable);
}

/// @brief replaces an existing variable in the current scope
void Scopes::replaceVariable(Variable* variable) {
  TRI_ASSERT(!_activeScopes.empty());
  TRI_ASSERT(variable != nullptr);

  _activeScopes.back()->addVariable(variable);
}

/// @brief checks whether a variable exists in any scope
bool Scopes::existsVariable(std::string_view name) const {
  return getVariable(name) != nullptr;
}

/// @brief return a variable by name - this respects the current scopes
Variable const* Scopes::getVariable(std::string_view name) const {
  TRI_ASSERT(!_activeScopes.empty());

  for (auto it = _activeScopes.rbegin(); it != _activeScopes.rend(); ++it) {
    auto variable = (*it)->getVariable(name);

    if (variable != nullptr) {
      return variable;
    }
  }

  return nullptr;
}

/// @brief return a variable by name - this respects the current scopes
Variable const* Scopes::getVariable(std::string_view name,
                                    bool allowSpecial) const {
  TRI_ASSERT(!_activeScopes.empty());

  for (auto it = _activeScopes.rbegin(); it != _activeScopes.rend(); ++it) {
    auto variable = (*it)->getVariable(name, allowSpecial);

    if (variable != nullptr) {
      return variable;
    }
  }

  return nullptr;
}

/// @brief get the $CURRENT variable
Variable const* Scopes::getCurrentVariable() const {
  if (_currentVariables.empty()) {
    // std::string_view NAME_CURRENT is not guaranteed to be null-terminated...
    // in order to pass it into a function that requires a null-terminated C
    // string, we need to create a temporary string
    std::string temp(Variable::NAME_CURRENT);
    THROW_ARANGO_EXCEPTION_PARAMS(TRI_ERROR_QUERY_VARIABLE_NAME_UNKNOWN,
                                  temp.c_str());
  }
  auto result = _currentVariables.back();
  TRI_ASSERT(result != nullptr);
  return result;
}

/// @brief stack a $CURRENT variable from the stack
void Scopes::stackCurrentVariable(Variable const* variable) {
  _currentVariables.emplace_back(variable);
}

/// @brief unregister the $CURRENT variable from the stack
void Scopes::unstackCurrentVariable() {
  TRI_ASSERT(!_currentVariables.empty());

  _currentVariables.pop_back();
}
