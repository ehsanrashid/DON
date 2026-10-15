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

#include "../utility/environment.h"

#if defined(__INTELLISENSE__)
    // Dummy thread-safety annotations for IntelliSense
    // clang-format off
    #define LOCK_RETURNED(x)
    #define CAPABILITY(x)
    #define GUARDED_BY(x)
    #define PT_GUARDED_BY(x)
    #define ACQUIRED_BEFORE(...)
    #define ACQUIRED_AFTER(...)
    #define ASSERT_CAPABILITY(x)
    #define ACQUIRE_CAPABILITY(...)
    #define TRY_ACQUIRE_CAPABILITY(...)
    #define RELEASE_CAPABILITY(...)
    #define REQUIRES_CAPABILITY(...)
    #define ASSERT_SHARED_CAPABILITY(x)
    #define ACQUIRE_SHARED_CAPABILITY(...)
    #define TRY_ACQUIRE_SHARED_CAPABILITY(...)
    #define RELEASE_SHARED_CAPABILITY(...)
    #define REQUIRES_SHARED_CAPABILITY(...)
    #define LOCKS_EXCLUDED(...)
    #define RELEASE_GENERIC_CAPABILITY(...)
    #define NO_THREAD_SAFETY_ANALYSIS
    #define REENTRANT_CAPABILITY
    #define LOCKABLE                            CAPABILITY("mutex")
    #define SCOPED_LOCKABLE
    // clang-format on
#else
    #include "../utility/attributes.h"

    // Enable compiler thread-safety attributes only for Clang, excluding SWIG.
    // These attributes have no effect when compiling with other compilers.
    #if defined(__clang__) && !defined(SWIG)
        #define THREAD_ATTRIBUTE(x) ATTRIBUTE(x)
    #else
        #define THREAD_ATTRIBUTE(x)
    #endif

    // clang-format off
    #define LOCK_RETURNED(x)                    THREAD_ATTRIBUTE(lock_returned(x))
    #define CAPABILITY(x)                       THREAD_ATTRIBUTE(capability(x))
    #define GUARDED_BY(x)                       THREAD_ATTRIBUTE(guarded_by(x))
    #define PT_GUARDED_BY(x)                    THREAD_ATTRIBUTE(pt_guarded_by(x))
    #define ACQUIRED_BEFORE(...)                THREAD_ATTRIBUTE(acquired_before(__VA_ARGS__))
    #define ACQUIRED_AFTER(...)                 THREAD_ATTRIBUTE(acquired_after(__VA_ARGS__))
    #define ASSERT_CAPABILITY(x)                THREAD_ATTRIBUTE(assert_capability(x))
    #define ACQUIRE_CAPABILITY(...)             THREAD_ATTRIBUTE(acquire_capability(__VA_ARGS__))
    #define TRY_ACQUIRE_CAPABILITY(...)         THREAD_ATTRIBUTE(try_acquire_capability(__VA_ARGS__))
    #define RELEASE_CAPABILITY(...)             THREAD_ATTRIBUTE(release_capability(__VA_ARGS__))
    #define REQUIRES_CAPABILITY(...)            THREAD_ATTRIBUTE(requires_capability(__VA_ARGS__))
    #define ASSERT_SHARED_CAPABILITY(x)         THREAD_ATTRIBUTE(assert_shared_capability(x))
    #define ACQUIRE_SHARED_CAPABILITY(...)      THREAD_ATTRIBUTE(acquire_shared_capability(__VA_ARGS__))
    #define TRY_ACQUIRE_SHARED_CAPABILITY(...)  THREAD_ATTRIBUTE(try_acquire_shared_capability(__VA_ARGS__))
    #define RELEASE_SHARED_CAPABILITY(...)      THREAD_ATTRIBUTE(release_shared_capability(__VA_ARGS__))
    #define REQUIRES_SHARED_CAPABILITY(...)     THREAD_ATTRIBUTE(requires_shared_capability(__VA_ARGS__))
    #define LOCKS_EXCLUDED(...)                 THREAD_ATTRIBUTE(locks_excluded(__VA_ARGS__))
    #define RELEASE_GENERIC_CAPABILITY(...)     THREAD_ATTRIBUTE(release_generic_capability(__VA_ARGS__))
    #define NO_THREAD_SAFETY_ANALYSIS           THREAD_ATTRIBUTE(no_thread_safety_analysis)
    #define REENTRANT_CAPABILITY                THREAD_ATTRIBUTE(reentrant_capability)
    #define LOCKABLE                            CAPABILITY("mutex")
    #define SCOPED_LOCKABLE                     THREAD_ATTRIBUTE(scoped_lockable)
    // clang-format on
#endif

#endif  // UTILITY_THREAD_ATTRIBUTES_H_INCLUDED
