/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef UTILITY_COMPARATOR_H_INCLUDED
#define UTILITY_COMPARATOR_H_INCLUDED

#include <string_view>

#include "../utility/types.h"

namespace DON {

// Define a custom case-insensitive hash
struct CaseInsensitiveHash final {
    usize operator()(std::string_view sv) const noexcept;
};
// Define a custom case-insensitive equality
struct CaseInsensitiveEqual final {
    bool operator()(std::string_view sv1, std::string_view sv2) const noexcept;
};
// Define a custom case-insensitive less
struct CaseInsensitiveLess final {
    bool operator()(std::string_view sv1, std::string_view sv2) const noexcept;
};

}  // namespace DON

#endif  // UTILITY_COMPARATOR_H_INCLUDED
