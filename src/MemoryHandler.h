#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _MEMORY_HANDLER_H_
#define _MEMORY_HANDLER_H_

#ifdef LPAGES

#   include "Type.h"

namespace MemoryHandler {

    extern void create_memory   (void **mem_ref, uint64_t size, uint8_t align);

    extern void free_memory     (void *mem);

    extern void initialize      ();

}

#endif // LPAGES

#endif // _MEMORY_HANDLER_H_
