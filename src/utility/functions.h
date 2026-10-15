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

#ifndef UTILITY_FUNCTIONS_H_INCLUDED
#define UTILITY_FUNCTIONS_H_INCLUDED

#include <string>

#include "../utility/types.h"

namespace DON {

// Convert a wide string to UTF-8.
std::string wstring_to_utf8(std::wstring_view wsv) noexcept;

fs::path utf8_to_path(std::string_view utf8) noexcept;

}  // namespace DON

#endif  // UTILITY_FUNCTIONS_H_INCLUDED
