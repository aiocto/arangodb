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
/// @author Dr. Frank Celler
////////////////////////////////////////////////////////////////////////////////

#pragma once

#include <ctime>
#include <string>

// safe localtime
void TRI_localtime(time_t, struct tm*);

// safe gmtime
void TRI_gmtime(time_t, struct tm*);

// safe timegm
time_t TRI_timegm(struct tm*);

// seconds with microsecond resolution
double TRI_microtime() noexcept;

namespace arangodb {
namespace utilities {
// return the current time as string in format "YYYY-MM-DDTHH:MM:SSZ"
std::string timeString(char sep = 'T', char fin = 'Z');

std::string hostname();
}  // namespace utilities
}  // namespace arangodb
