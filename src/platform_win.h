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

#ifndef PLATFORM_WIN_H_INCLUDED
#define PLATFORM_WIN_H_INCLUDED

// Prevent inclusion of min/max Windows macros to avoid conflicts
#if !defined(NOMINMAX)
    #define NOMINMAX
#endif
// Prevent inclusion of rarely-used Windows APIs to speed up compilation
#if !defined(WIN32_LEAN_AND_MEAN)
    #define WIN32_LEAN_AND_MEAN
#endif

// Require Windows 7 or later API definitions
#include <sdkddkver.h>
#if defined(_WIN32_WINNT) && _WIN32_WINNT < _WIN32_WINNT_WIN7
    #undef _WIN32_WINNT
#endif
#if !defined(_WIN32_WINNT)
    // Force to include needed API prototypes
    // [_WIN32_WINNT_WIN7 or _WIN32_WINNT_WIN10]
    #define _WIN32_WINNT _WIN32_WINNT_WIN7
#endif

// Avoid UNICODE macro conflicts
#undef UNICODE

#include <windows.h>

// Clean up unwanted macros
#undef small

#endif  // PLATFORM_WIN_H_INCLUDED
