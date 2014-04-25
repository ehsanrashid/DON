#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _MEMORY_HANDLER_H_INC_
#define _MEMORY_HANDLER_H_INC_

#ifdef LPAGES

#   include "Type.h"

namespace MemoryHandler {

    extern void create_memory   (void *&mem_ref, u64 mem_size, u08 align);

    extern void   free_memory   (void *mem);

    extern void initialize      ();

}

#endif // LPAGES

#endif // _MEMORY_HANDLER_H_INC_
