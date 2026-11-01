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

#include "../utility/comparator.h"

#include <algorithm>
#include <functional>
#include <string>

#include "../utility/functions.h"
#include "../utility/types.h"

namespace DON {

usize CaseInsensitiveHash::operator()(const std::string_view sv) const noexcept {
    return std::hash<std::string_view>{}(lower_case(std::string{sv}));
}

bool CaseInsensitiveEqual::operator()(const std::string_view sv1,
                                      const std::string_view sv2) const noexcept {
    return sv1.size() == sv2.size()
        && std::equal(sv1.begin(), sv1.end(), sv2.begin(), sv2.end(),
                      [](const char ch1, const char ch2) noexcept {
                          return lower_case(ch1) == lower_case(ch2);
                      });
}

bool CaseInsensitiveLess::operator()(const std::string_view sv1,
                                     const std::string_view sv2) const noexcept {
    return std::lexicographical_compare(
      sv1.begin(), sv1.end(), sv2.begin(), sv2.end(),
      [](const char ch1, const char ch2) noexcept { return lower_case(ch1) < lower_case(ch2); });
}

}  // namespace DON
