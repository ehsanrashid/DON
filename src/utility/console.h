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

#ifndef UTILITY_CONSOLE_H_INCLUDED
#define UTILITY_CONSOLE_H_INCLUDED

#include "../utility/streambuf.h"

namespace DON {
namespace ConsoleColor {

// Reset
inline constexpr const char* RESET = "\033[0m";

// Default foreground and background colors
inline constexpr const char* DEFAULT_FOREGROUND = "\033[39m";
inline constexpr const char* DEFAULT_BACKGROUND = "\033[49m";

// Foreground colors
inline constexpr const char* BLACK   = "\033[30m";
inline constexpr const char* RED     = "\033[31m";
inline constexpr const char* GREEN   = "\033[32m";
inline constexpr const char* YELLOW  = "\033[33m";
inline constexpr const char* BLUE    = "\033[34m";
inline constexpr const char* MAGENTA = "\033[35m";
inline constexpr const char* CYAN    = "\033[36m";
inline constexpr const char* WHITE   = "\033[37m";

// Bright foreground colors
inline constexpr const char* BRIGHT_BLACK   = "\033[90m";
inline constexpr const char* BRIGHT_RED     = "\033[91m";
inline constexpr const char* BRIGHT_GREEN   = "\033[92m";
inline constexpr const char* BRIGHT_YELLOW  = "\033[93m";
inline constexpr const char* BRIGHT_BLUE    = "\033[94m";
inline constexpr const char* BRIGHT_MAGENTA = "\033[95m";
inline constexpr const char* BRIGHT_CYAN    = "\033[96m";
inline constexpr const char* BRIGHT_WHITE   = "\033[97m";

// Background colors
inline constexpr const char* BG_BLACK   = "\033[40m";
inline constexpr const char* BG_RED     = "\033[41m";
inline constexpr const char* BG_GREEN   = "\033[42m";
inline constexpr const char* BG_YELLOW  = "\033[43m";
inline constexpr const char* BG_BLUE    = "\033[44m";
inline constexpr const char* BG_MAGENTA = "\033[45m";
inline constexpr const char* BG_CYAN    = "\033[46m";
inline constexpr const char* BG_WHITE   = "\033[47m";

// Bright background colors
inline constexpr const char* BG_BRIGHT_BLACK   = "\033[100m";
inline constexpr const char* BG_BRIGHT_RED     = "\033[101m";
inline constexpr const char* BG_BRIGHT_GREEN   = "\033[102m";
inline constexpr const char* BG_BRIGHT_YELLOW  = "\033[103m";
inline constexpr const char* BG_BRIGHT_BLUE    = "\033[104m";
inline constexpr const char* BG_BRIGHT_MAGENTA = "\033[105m";
inline constexpr const char* BG_BRIGHT_CYAN    = "\033[106m";
inline constexpr const char* BG_BRIGHT_WHITE   = "\033[107m";

// Text styles
inline constexpr const char* BOLD             = "\033[1m";
inline constexpr const char* DIM              = "\033[2m";
inline constexpr const char* ITALIC           = "\033[3m";
inline constexpr const char* UNDERLINE        = "\033[4m";
inline constexpr const char* BLINK            = "\033[5m";
inline constexpr const char* REVERSE          = "\033[7m";
inline constexpr const char* STRIKETHROUGH    = "\033[9m";
inline constexpr const char* DOUBLE_UNDERLINE = "\033[21m";
inline constexpr const char* FRAMED           = "\033[51m";
inline constexpr const char* ENCIRCLED        = "\033[52m";
inline constexpr const char* OVERLINE         = "\033[53m";
inline constexpr const char* SUPERSCRIPT      = "\033[73m";
inline constexpr const char* SUBSCRIPT        = "\033[74m";

// Individual style resets
inline constexpr const char* RESET_BOLD_DIM              = "\033[22m";
inline constexpr const char* RESET_ITALIC                = "\033[23m";
inline constexpr const char* RESET_UNDERLINE             = "\033[24m";
inline constexpr const char* RESET_BLINK                 = "\033[25m";
inline constexpr const char* RESET_REVERSE               = "\033[27m";
inline constexpr const char* RESET_STRIKETHROUGH         = "\033[29m";
inline constexpr const char* RESET_FRAME_ENCIRCLED       = "\033[54m";
inline constexpr const char* RESET_OVERLINE              = "\033[55m";
inline constexpr const char* RESET_SUPERSCRIPT_SUBSCRIPT = "\033[75m";

}  // namespace ConsoleColor

void set_console_utf8() noexcept;

class ColorBuf;

class ConsoleColors final {
   public:
    explicit ConsoleColors(const char* outColor = ConsoleColor::BRIGHT_BLUE,
                           const char* errColor = ConsoleColor::BRIGHT_RED) noexcept;

    ConsoleColors(const ConsoleColors&)            = delete;
    ConsoleColors& operator=(const ConsoleColors&) = delete;
    ConsoleColors(ConsoleColors&&)                 = delete;
    ConsoleColors& operator=(ConsoleColors&&)      = delete;

    ~ConsoleColors() noexcept;

    void set_colors(const char* outColor = ConsoleColor::BRIGHT_BLUE,
                    const char* errColor = ConsoleColor::BRIGHT_RED) noexcept;

    void restore_colors() noexcept;

   private:
    ColorBuf outColorBuf;
    ColorBuf errColorBuf;

    bool outColorActive = false;
    bool errColorActive = false;
};

}  // namespace DON

#endif  // UTILITY_CONSOLE_H_INCLUDED
