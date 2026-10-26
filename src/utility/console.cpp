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
    #include <io.h>
    #include <cstdio>
#else
    #include <unistd.h>
#endif

#include <iostream>

#include "../utility/streambuf.h"

namespace DON {

Console::Console() noexcept = default;

Console::~Console() noexcept { restore(); }

bool Console::initialize() noexcept {
    if (initialized)
        return true;

    const bool utf8 = set_utf8();

#if defined(_WIN32)
    enable_virtual_terminal();
#endif

    initialized = true;
    return utf8;
}

void Console::restore() noexcept {
    // Restore stream buffers before the ColorBuf objects are destroyed.
    restore_colors();

#if defined(_WIN32)
    // Restore the original console modes only when they were saved.
    if (outputModeSaved)
    {
        const HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);

        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            (void) ::SetConsoleMode(handle, outputMode);

        outputModeSaved = false;
    }

    if (errorModeSaved)
    {
        const HANDLE handle = ::GetStdHandle(STD_ERROR_HANDLE);

        if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            (void) ::SetConsoleMode(handle, errorMode);

        errorModeSaved = false;
    }

    // Restore original code pages.
    if (inputCodePageSaved)
    {
        (void) ::SetConsoleCP(inputCodePage);
        inputCodePageSaved = false;
    }

    if (outputCodePageSaved)
    {
        (void) ::SetConsoleOutputCP(outputCodePage);
        outputCodePageSaved = false;
    }
#endif

    utf8Enabled = false;
    initialized = false;
}

bool Console::is_terminal() const noexcept {
#if defined(_WIN32)
    return ::_isatty(::_fileno(stdout)) != 0;
#else
    return ::isatty(STDOUT_FILENO) != 0;
#endif
}

bool Console::set_utf8() noexcept {
#if defined(_WIN32)
    // Capture the original settings once, before modifying them.
    if (!inputCodePageSaved)
    {
        const UINT codePage = ::GetConsoleCP();

        if (codePage == 0)
            return false;

        inputCodePage      = codePage;
        inputCodePageSaved = true;
    }

    if (!outputCodePageSaved)
    {
        const UINT codePage = ::GetConsoleOutputCP();

        if (codePage == 0)
            return false;

        outputCodePage      = codePage;
        outputCodePageSaved = true;
    }

    const bool inputOk = ::SetConsoleCP(CP_UTF8) != 0;

    if (!inputOk)
    {
        utf8Enabled = false;
        return utf8Enabled;
    }

    const bool outputOk = ::SetConsoleOutputCP(CP_UTF8) != 0;

    if (!outputOk)
    {
        (void) ::SetConsoleCP(inputCodePage);
        utf8Enabled = false;
        return utf8Enabled;
    }

    utf8Enabled = true;
#else
    // POSIX terminals use the active locale/environment for encoding.
    // This function does not change the process locale.
    utf8Enabled = true;
#endif
    return utf8Enabled;
}

void Console::enable_virtual_terminal() noexcept {
#if defined(_WIN32)
    const auto enable = [](DWORD handleId, DWORD& mode, bool& modeSaved) noexcept {
        const HANDLE handle = ::GetStdHandle(handleId);

        if (handle == nullptr || handle == INVALID_HANDLE_VALUE)
            return;

        DWORD mode_ = 0;

        if (!::GetConsoleMode(handle, &mode_))
            return;

        if (!modeSaved)
        {
            mode      = mode_;
            modeSaved = true;
        }

        (void) ::SetConsoleMode(handle, mode_ | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    };

    enable(STD_OUTPUT_HANDLE, outputMode, outputModeSaved);
    enable(STD_ERROR_HANDLE, errorMode, errorModeSaved);
#endif
}

void Console::set_colors(const char* const outColor, const char* const errColor) noexcept {
    // Preserve the underlying buffers before removing the current color buffers.
    std::streambuf* const outBuf = outColorActive ? outColorBuf.buf() : std::cout.rdbuf();

    std::streambuf* const errBuf = errColorActive ? errColorBuf.buf() : std::cerr.rdbuf();

    // Remove existing color buffers.
    restore_colors();

    // Enable VT processing when at least one stream needs colors.
    if (outColor != nullptr || errColor != nullptr)
        enable_virtual_terminal();

    // Install the output color buffer.
    if (outColor != nullptr)
    {
        outColorBuf.set(outBuf, outColor);
        std::cout.rdbuf(&outColorBuf);
        outColorActive = true;
    }

    // Install the error color buffer.
    if (errColor != nullptr)
    {
        errColorBuf.set(errBuf, errColor);
        std::cerr.rdbuf(&errColorBuf);
        errColorActive = true;
    }
}

// Restores the underlying stream buffers.
void Console::restore_colors() noexcept {
    if (outColorActive)
    {
        std::cout.rdbuf(outColorBuf.buf());
        outColorActive = false;
    }

    if (errColorActive)
    {
        std::cerr.rdbuf(errColorBuf.buf());
        errColorActive = false;
    }
}

// ------------------

Console& console() noexcept {
    static Console console;
    return console;
}

}  // namespace DON
