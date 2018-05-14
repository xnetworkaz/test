//
// Copyright 2017 The Abseil Authors.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
// -----------------------------------------------------------------------------
// File: match.h
// -----------------------------------------------------------------------------
//
// This file contains simple utilities for performing std::string matching checks.
// All of these function parameters are specified as `absl::string_view`,
// meaning that these functions can accept `std::string`, `absl::string_view` or
// nul-terminated C-style strings.
//
// Examples:
//   std::string s = "foo";
//   absl::string_view sv = "f";
//   assert(absl::StrContains(s, sv));
//
// Note: The order of parameters in these functions is designed to mimic the
// order an equivalent member function would exhibit;
// e.g. `s.Contains(x)` ==> `absl::StrContains(s, x).
#ifndef ABSL_STRINGS_MATCH_H_
#define ABSL_STRINGS_MATCH_H_

#include <cstring>

#include "absl/strings/string_view.h"

namespace absl {

// StrContains()
//
// Returns whether a given std::string `haystack` contains the substring `needle`.
inline bool StrContains(absl::string_view haystack, absl::string_view needle) {
  return static_cast<absl::string_view::size_type>(haystack.find(needle, 0)) !=
         haystack.npos;
}

// StartsWith()
//
// Returns whether a given std::string `text` begins with `prefix`.
inline bool StartsWith(absl::string_view text, absl::string_view prefix) {
  return prefix.empty() ||
         (text.size() >= prefix.size() &&
          memcmp(text.data(), prefix.data(), prefix.size()) == 0);
}

// EndsWith()
//
// Returns whether a given std::string `text` ends with `suffix`.
inline bool EndsWith(absl::string_view text, absl::string_view suffix) {
  return suffix.empty() ||
         (text.size() >= suffix.size() &&
          memcmp(text.data() + (text.size() - suffix.size()), suffix.data(),
                 suffix.size()) == 0
         );
}

// StartsWithIgnoreCase()
//
// Returns whether a given std::string `text` starts with `starts_with`, ignoring
// case in the comparison.
bool StartsWithIgnoreCase(absl::string_view text, absl::string_view prefix);

// EndsWithIgnoreCase()
//
// Returns whether a given std::string `text` ends with `ends_with`, ignoring case
// in the comparison.
bool EndsWithIgnoreCase(absl::string_view text, absl::string_view suffix);

}  // namespace absl

#endif  // ABSL_STRINGS_MATCH_H_
