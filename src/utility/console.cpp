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

#include "../utility/console.h"

#if defined(_WIN32)
    #include "../utility/platform_win.h"
#endif

#include "../utility/streambuf.h"

namespace DON {

void set_console_utf8() noexcept {
#if defined(_WIN32)
    ::SetConsoleCP(CP_UTF8);
    ::SetConsoleOutputCP(CP_UTF8);
#endif
}

}  // namespace DON
