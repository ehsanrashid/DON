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

// Standalone NNUE embedding for universal binary builds

#include "../evaluate.h"
#include "../misc.h"

#if defined(UNIVERSAL_BINARY_MACOS_X86_64_SLICE)

// In a macOS universal binary the network is embedded only in the arm64 slice,
// and the x86-64 slice mmaps it from the arm64 slice.

    #include <fcntl.h>
    #include <limits.h>
    #include <mach-o/dyld.h>
    #include <stdlib.h>
    #include <sys/mman.h>
    #include <unistd.h>

// Must be kept in sync with patch_x86_64_slice.sh
extern const volatile DON::u64 gUniversalNNUEOffset = DON::u64{0xCAFE0FF5E70FF5E7};
extern const volatile DON::u64 gUniversalNNUESize   = DON::u64{0xCAFE512ECAFE512E};

namespace {

const unsigned char* map_embedded_nnue() noexcept {
    char     path[PATH_MAX];
    DON::u32 len = sizeof(path);
    if (_NSGetExecutablePath(path, &len) != 0)
        return nullptr;

    char        resolved[PATH_MAX];
    const char* file = ::realpath(path, resolved) ? resolved : path;

    int fd = ::open(file, O_RDONLY | O_CLOEXEC);
    if (!DON::is_valid_fd(fd))
        return nullptr;

    const auto close_fd = [&fd]() noexcept { ::close(fd); };

    const long systemPageSize = ::sysconf(_SC_PAGESIZE);
    if (systemPageSize == -1)
    {
        close_fd();
        return nullptr;
    }

    // Align down to page size for mmap
    const DON::u64 pageSize = DON::u64(systemPageSize);
    const DON::u64 base     = gUniversalNNUEOffset & ~(pageSize - 1);
    const DON::u64 pad      = gUniversalNNUEOffset - base;

    void* mappedMemory = ::mmap(nullptr, DON::usize(gUniversalNNUESize + pad), PROT_READ,
                                MAP_PRIVATE, fd, off_t(base));

    close_fd();

    if (mappedMemory == MAP_FAILED)
        return nullptr;

    return reinterpret_cast<const unsigned char*>(mappedMemory) + pad;
}
}  // namespace

extern const unsigned char* const gEmbeddedNNUEData = map_embedded_nnue();
extern const unsigned int         gEmbeddedNNUESize = (unsigned int) (gUniversalNNUESize);

#else
    #if defined(__has_embed)
// C++23 #embed: embeds the binary data directly
extern const unsigned char gEmbeddedNNUEData[] = {
        #embed EvalFileDefaultName
};

const unsigned int padding = 0;
    #else
// Fallback for compilers without #embed support
extern const unsigned char gEmbeddedNNUEData[] =
        #include "network_dump.inc"
  ;

const unsigned int padding = 1;  // Trailing NULL byte
    #endif
extern const unsigned int gEmbeddedNNUESize = sizeof(gEmbeddedNNUEData) - padding;

#endif
