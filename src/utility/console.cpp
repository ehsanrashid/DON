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

#include <iostream>

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

// ------------------

ConsoleColors::ConsoleColors(const char* const outColor, const char* const errColor) noexcept {
    set_colors(outColor, errColor);
}

ConsoleColors::~ConsoleColors() noexcept { restore_colors(); }

void ConsoleColors::set_colors(const char* const outColor, const char* const errColor) noexcept {
    // Recover the underlying stream buffers
    auto* const outBuffer = outColorBuf != nullptr ? outColorBuf->buffer() : std::cout.rdbuf();
    auto* const errBuffer = errColorBuf != nullptr ? errColorBuf->buffer() : std::cerr.rdbuf();

    // Restore the underlying stream buffers and destroy the existing color buffers
    restore_colors();

#if defined(_WIN32)
    // Enable Windows VT processing
    const auto enable_virtual_terminal = [](const DWORD handleId) noexcept {
        const HANDLE handle = ::GetStdHandle(handleId);
        DWORD        mode;

        if (handle != INVALID_HANDLE_VALUE && handle != nullptr && ::GetConsoleMode(handle, &mode))
            (void) ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    };

    if (outColor != nullptr)
        enable_virtual_terminal(STD_OUTPUT_HANDLE);

    if (errColor != nullptr)
        enable_virtual_terminal(STD_ERROR_HANDLE);
#endif

    // Create and install the new color buffers
    if (outColor != nullptr)
    {
        outColorBuf = std::make_unique<ColorBuf>(outBuffer, outColor);
        std::cout.rdbuf(outColorBuf.get());
    }

    if (errColor != nullptr)
    {
        errColorBuf = std::make_unique<ColorBuf>(errBuffer, errColor);
        std::cerr.rdbuf(errColorBuf.get());
    }
}

// Restores the underlying stream buffers and destroys the color buffers
void ConsoleColors::restore_colors() noexcept {
    if (outColorBuf != nullptr)
    {
        std::cout.rdbuf(outColorBuf->buffer());
        outColorBuf.reset();
    }

    if (errColorBuf != nullptr)
    {
        std::cerr.rdbuf(errColorBuf->buffer());
        errColorBuf.reset();
    }
}

// ------------------

}  // namespace DON
