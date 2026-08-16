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

#if defined(UNIVERSAL_BINARY_MACOS_X86_SLICE)

// In a macOS universal binary the network is embedded only in the arm64 slice,
// and the x86-64 slice mmaps it from the arm64 slice.

    #include <climits>
    #include <cstdint>
    #include <fcntl.h>
    #include <mach-o/dyld.h>
    #include <stdlib.h>
    #include <sys/mman.h>
    #include <unistd.h>

// Must be kept in sync with patch_x86_slice.sh
extern const volatile DON::u64 gNNUEUniversalOffset = 0xCAFE0FF5E70FF5E7ULL;
extern const volatile DON::u64 gNNUEUniversalSize   = 0xCAFE512ECAFE512EULL;

static const unsigned char* map_embedded_nnue() noexcept {
    char     path[PATH_MAX];
    DON::u32 len = sizeof(path);
    if (_NSGetExecutablePath(path, &len) != 0)
        return nullptr;

    char        resolved[PATH_MAX];
    const char* file = realpath(path, resolved) ? resolved : path;

    int fd = open(file, O_RDONLY);
    if (fd < 0)
        return nullptr;

    // Align down to page size for mmap
    const DON::u64 pageSize = DON::u64(sysconf(_SC_PAGESIZE));
    const DON::u64 base     = gNNUEUniversalOffset & ~(pageSize - 1);
    const DON::u64 pad      = gNNUEUniversalOffset - base;

    void* p =
      mmap(nullptr, size_t(gNNUEUniversalSize + pad), PROT_READ, MAP_PRIVATE, fd, off_t(base));
    close(fd);
    if (p == MAP_FAILED)
        return nullptr;

    return reinterpret_cast<const unsigned char*>(p) + pad;
}

extern const unsigned char* const gNNUEEmbeddedData = map_embedded_nnue();
extern const unsigned int         gNNUEEmbeddedSize = static_cast<unsigned int>(gNNUEUniversalSize);

#else
    #if defined(__has_embed)
extern const unsigned char gNNUEEmbeddedData[] = {
        #embed EvalFileDefaultName
};

const unsigned int padding = 0;
    #else
extern const unsigned char gNNUEEmbeddedData[] =
        #include "network_dump.inc"
  ;

const unsigned int padding = 1;  // Trailing NULL byte
    #endif

extern const unsigned int gNNUEEmbeddedSize = sizeof(gNNUEEmbeddedData) - padding;

#endif
