#ifndef _MEMORY_HANDLER_H_INC_
#define _MEMORY_HANDLER_H_INC_

#ifdef LPAGES

#   include "Type.h"

namespace Memory {

    extern void alloc_memory (void *&mem_ref, u64 mem_size, u32 alignment);

    extern void  free_memory (void *mem);

    extern void initialize   ();

}

#endif // LPAGES

#endif // _MEMORY_HANDLER_H_INC_
