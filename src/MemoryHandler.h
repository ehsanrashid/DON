#pragma once

#if defined(LPAGES)

#include "Type.h"

namespace Memory {

    extern void alloc_memory(void*&, size_t, u32);

    extern void free_memory(void*);

    extern void initialize();
    extern void deinitialize();

}

#endif // LPAGES
