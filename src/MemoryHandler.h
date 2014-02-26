//#pragma once
#ifndef MEMORY_HANDLER_H_
#define MEMORY_HANDLER_H_

#include "Type.h"

namespace Memoryhandler {

    extern void setup_privileges (const char* privilege = "SeLockMemoryPrivilege", bool enable = true);

    extern void create_memory (void **mem_ref, uint64_t size, uint32_t align);

    extern void free_memory (void *mem_ref);

}

#endif
