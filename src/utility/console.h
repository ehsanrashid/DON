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

#if defined(_WIN32)
    #include "../utility/platform_win.h"
#endif

#include "../utility/streambuf.h"

namespace DON {
namespace ConsoleColor {

// Reset
inline constexpr const char* const RESET = "\033[0m";

// Default foreground and background colors
inline constexpr const char* const DEFAULT_FOREGROUND = "\033[39m";
inline constexpr const char* const DEFAULT_BACKGROUND = "\033[49m";

// Foreground colors
inline constexpr const char* const BLACK   = "\033[30m";
inline constexpr const char* const RED     = "\033[31m";
inline constexpr const char* const GREEN   = "\033[32m";
inline constexpr const char* const YELLOW  = "\033[33m";
inline constexpr const char* const BLUE    = "\033[34m";
inline constexpr const char* const MAGENTA = "\033[35m";
inline constexpr const char* const CYAN    = "\033[36m";
inline constexpr const char* const WHITE   = "\033[37m";

// Bright foreground colors
inline constexpr const char* const BRIGHT_BLACK   = "\033[90m";
inline constexpr const char* const BRIGHT_RED     = "\033[91m";
inline constexpr const char* const BRIGHT_GREEN   = "\033[92m";
inline constexpr const char* const BRIGHT_YELLOW  = "\033[93m";
inline constexpr const char* const BRIGHT_BLUE    = "\033[94m";
inline constexpr const char* const BRIGHT_MAGENTA = "\033[95m";
inline constexpr const char* const BRIGHT_CYAN    = "\033[96m";
inline constexpr const char* const BRIGHT_WHITE   = "\033[97m";

// Background colors
inline constexpr const char* const BG_BLACK   = "\033[40m";
inline constexpr const char* const BG_RED     = "\033[41m";
inline constexpr const char* const BG_GREEN   = "\033[42m";
inline constexpr const char* const BG_YELLOW  = "\033[43m";
inline constexpr const char* const BG_BLUE    = "\033[44m";
inline constexpr const char* const BG_MAGENTA = "\033[45m";
inline constexpr const char* const BG_CYAN    = "\033[46m";
inline constexpr const char* const BG_WHITE   = "\033[47m";

// Bright background colors
inline constexpr const char* const BG_BRIGHT_BLACK   = "\033[100m";
inline constexpr const char* const BG_BRIGHT_RED     = "\033[101m";
inline constexpr const char* const BG_BRIGHT_GREEN   = "\033[102m";
inline constexpr const char* const BG_BRIGHT_YELLOW  = "\033[103m";
inline constexpr const char* const BG_BRIGHT_BLUE    = "\033[104m";
inline constexpr const char* const BG_BRIGHT_MAGENTA = "\033[105m";
inline constexpr const char* const BG_BRIGHT_CYAN    = "\033[106m";
inline constexpr const char* const BG_BRIGHT_WHITE   = "\033[107m";

// Text styles
inline constexpr const char* const BOLD             = "\033[1m";
inline constexpr const char* const DIM              = "\033[2m";
inline constexpr const char* const ITALIC           = "\033[3m";
inline constexpr const char* const UNDERLINE        = "\033[4m";
inline constexpr const char* const BLINK            = "\033[5m";
inline constexpr const char* const REVERSE          = "\033[7m";
inline constexpr const char* const STRIKETHROUGH    = "\033[9m";
inline constexpr const char* const DOUBLE_UNDERLINE = "\033[21m";
inline constexpr const char* const FRAMED           = "\033[51m";
inline constexpr const char* const ENCIRCLED        = "\033[52m";
inline constexpr const char* const OVERLINE         = "\033[53m";
inline constexpr const char* const SUPERSCRIPT      = "\033[73m";
inline constexpr const char* const SUBSCRIPT        = "\033[74m";

// Individual style resets
inline constexpr const char* const RESET_BOLD_DIM              = "\033[22m";
inline constexpr const char* const RESET_ITALIC                = "\033[23m";
inline constexpr const char* const RESET_UNDERLINE             = "\033[24m";
inline constexpr const char* const RESET_BLINK                 = "\033[25m";
inline constexpr const char* const RESET_REVERSE               = "\033[27m";
inline constexpr const char* const RESET_STRIKETHROUGH         = "\033[29m";
inline constexpr const char* const RESET_FRAME_ENCIRCLED       = "\033[54m";
inline constexpr const char* const RESET_OVERLINE              = "\033[55m";
inline constexpr const char* const RESET_SUPERSCRIPT_SUBSCRIPT = "\033[75m";

}  // namespace ConsoleColor

class Console final {
   public:
    Console() noexcept;
    ~Console() noexcept;

    Console(const Console&) noexcept            = delete;
    Console& operator=(const Console&) noexcept = delete;
    Console(Console&&) noexcept                 = delete;
    Console& operator=(Console&&) noexcept      = delete;

    // Console initialization and configuration.
    [[nodiscard]] bool initialize() noexcept;
    void               restore() noexcept;

    // Terminal capabilities.
    [[nodiscard]] bool is_terminal() const noexcept;

    // Encoding.
    [[nodiscard]] bool set_utf8() noexcept;

    void enable_virtual_terminal() noexcept;

    // Color control.
    void set_colors(const char* outColor = ConsoleColor::BRIGHT_BLUE,
                    const char* errColor = ConsoleColor::BRIGHT_RED) noexcept;
    void restore_colors() noexcept;


   private:
    bool initialized = false;
    bool utf8Enabled = false;

    ColorBuf outColorBuf;
    ColorBuf errColorBuf;

    bool outColorActive = false;
    bool errColorActive = false;

#if defined(_WIN32)
    UINT  inputCodePage  = 0;
    UINT  outputCodePage = 0;
    DWORD outputMode     = 0;
    DWORD errorMode      = 0;

    bool inputCodePageSaved  = false;
    bool outputCodePageSaved = false;
    bool outputModeSaved     = false;
    bool errorModeSaved      = false;
#endif
};

Console& console() noexcept;

}  // namespace DON

#endif  // UTILITY_CONSOLE_H_INCLUDED
