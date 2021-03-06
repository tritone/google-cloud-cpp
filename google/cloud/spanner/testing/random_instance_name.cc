// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "google/cloud/spanner/testing/random_instance_name.h"
#include <chrono>
#include <ctime>

namespace google {
namespace cloud {
namespace spanner_testing {
inline namespace SPANNER_CLIENT_NS {
/**
 * Generate a random instance name for InstanceAdminClient CRUD tests.
 */
std::string RandomInstanceName(
    google::cloud::internal::DefaultPRNG& generator) {
  // An instance ID must be between 2 and 64 characters, fitting the regular
  // expression `[a-z][-a-z0-9]*[a-z0-9]`
  std::size_t const max_size = 64;
  auto now = std::chrono::system_clock::now();
  auto time_t = std::chrono::system_clock::to_time_t(now);
  std::string date = "1973-03-01";
  // The standard C++ function to convert time_t to a struct tm is not thread
  // safe (it holds global storage), use some OS specific stuff here:
  std::tm tm{};
#if _WIN32
  gmtime_s(&tm, &time_t);
#else
  gmtime_r(&time_t, &tm);
#endif  // _WIN32
  std::strftime(&date[0], date.size() + 1, "%Y-%m-%d", &tm);
  std::string prefix = "temporary-instance-" + date + "-";
  auto size = static_cast<int>(max_size - 1 - prefix.size());
  return prefix +
         google::cloud::internal::Sample(
             generator, size, "abcdefghijlkmnopqrstuvwxyz012345689-") +
         google::cloud::internal::Sample(generator, 1,
                                         "abcdefghijlkmnopqrstuvwxyz");
}

}  // namespace SPANNER_CLIENT_NS
}  // namespace spanner_testing
}  // namespace cloud
}  // namespace google
