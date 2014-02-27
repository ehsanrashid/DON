//#pragma once
#ifndef MEMORY_HANDLER_H_
#define MEMORY_HANDLER_H_

#ifdef LPAGES

#include "Type.h"

// disable macros min() and max()
#   ifndef  NOMINMAX
#       define NOMINMAX
#   endif
#   ifndef  WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif

#   include <windows.h>

#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN

namespace Memoryhandler {

    extern void setup_privileges (const TCHAR* psz_privilege, bool enable = true);

    extern void create_memory   (void **mem_ref, uint64_t size, uint8_t align);

    extern void free_memory     (void *mem);

}

#endif

#endif
