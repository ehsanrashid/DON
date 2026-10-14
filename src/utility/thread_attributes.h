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

#ifndef UTILITY_THREAD_ATTRIBUTES_H_INCLUDED
#define UTILITY_THREAD_ATTRIBUTES_H_INCLUDED

#include "../utility/attributes.h"

#if defined(__INTELLISENSE__)
    // Dummy thread-safety annotations for IntelliSense.
    #define ASSERT_CAPABILITY(x)
    #define ASSERT_SHARED_CAPABILITY(x)
    #define RETURN_CAPABILITY(x)
    #define SCOPED_CAPABILITY
    #define CAPABILITY(x)
    #define GUARDED_BY(x)
    #define PT_GUARDED_BY(x)
    #define ACQUIRED_BEFORE(...)
    #define ACQUIRED_AFTER(...)
    #define REQUIRES(...)
    #define REQUIRES_SHARED(...)
    #define ACQUIRE(...)
    #define TRY_ACQUIRE(...)
    #define RELEASE(...)
    #define ACQUIRE_SHARED(...)
    #define TRY_ACQUIRE_SHARED(...)
    #define RELEASE_SHARED(...)
    #define EXCLUDES(...)
    #define RELEASE_GENERIC(...)
    #define LOCKABLE CAPABILITY("mutex")
    #define NO_THREAD_SAFETY_ANALYSIS
    #define REENTRANT_CAPABILITY

#else
    // Enable compiler thread-safety attributes only for Clang, excluding SWIG.
    // These attributes have no effect when compiling with other compilers.
    #if defined(__clang__) && !defined(SWIG)
        #define THREAD_ATTRIBUTE(x) ATTRIBUTE(x)
    #else
        #define THREAD_ATTRIBUTE(x)
    #endif

    // clang-format off
    #define ASSERT_CAPABILITY(x)        THREAD_ATTRIBUTE(assert_capability(x))
    #define ASSERT_SHARED_CAPABILITY(x) THREAD_ATTRIBUTE(assert_shared_capability(x))
    #define RETURN_CAPABILITY(x)        THREAD_ATTRIBUTE(lock_returned(x))
    #define SCOPED_CAPABILITY           THREAD_ATTRIBUTE(scoped_lockable)
    #define CAPABILITY(x)               THREAD_ATTRIBUTE(capability(x))
    #define GUARDED_BY(x)               THREAD_ATTRIBUTE(guarded_by(x))
    #define PT_GUARDED_BY(x)            THREAD_ATTRIBUTE(pt_guarded_by(x))
    #define ACQUIRED_BEFORE(...)        THREAD_ATTRIBUTE(acquired_before(__VA_ARGS__))
    #define ACQUIRED_AFTER(...)         THREAD_ATTRIBUTE(acquired_after(__VA_ARGS__))
    #define REQUIRES(...)               THREAD_ATTRIBUTE(requires_capability(__VA_ARGS__))
    #define REQUIRES_SHARED(...)        THREAD_ATTRIBUTE(requires_shared_capability(__VA_ARGS__))
    #define ACQUIRE(...)                THREAD_ATTRIBUTE(acquire_capability(__VA_ARGS__))
    #define TRY_ACQUIRE(...)            THREAD_ATTRIBUTE(try_acquire_capability(__VA_ARGS__))
    #define RELEASE(...)                THREAD_ATTRIBUTE(release_capability(__VA_ARGS__))
    #define ACQUIRE_SHARED(...)         THREAD_ATTRIBUTE(acquire_shared_capability(__VA_ARGS__))
    #define TRY_ACQUIRE_SHARED(...)     THREAD_ATTRIBUTE(try_acquire_shared_capability(__VA_ARGS__))
    #define RELEASE_SHARED(...)         THREAD_ATTRIBUTE(release_shared_capability(__VA_ARGS__))
    #define EXCLUDES(...)               THREAD_ATTRIBUTE(locks_excluded(__VA_ARGS__))
    #define RELEASE_GENERIC(...)        THREAD_ATTRIBUTE(release_generic_capability(__VA_ARGS__))
    #define LOCKABLE                    CAPABILITY("mutex")
    #define NO_THREAD_SAFETY_ANALYSIS   THREAD_ATTRIBUTE(no_thread_safety_analysis)
    #define REENTRANT_CAPABILITY        THREAD_ATTRIBUTE(reentrant_capability)
    // clang-format on

#endif

#endif  // UTILITY_THREAD_ATTRIBUTES_H_INCLUDED
