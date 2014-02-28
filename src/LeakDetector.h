#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _LEAK_DETECTOR_H_INC_
#define _LEAK_DETECTOR_H_INC_

#include "Type.h"

namespace LeakDetector {

    extern void* xmalloc (size_t size, const char filename[], uint32_t line_no);
    extern void* xcalloc (size_t count, size_t size, const char filename[], uint32_t line_no);
    extern void  xfree (void *mem_ref);

    extern void report_memleakage ();

}

#define LEN_FILENAME        256
#define FILE_OUTPUT         "leak_info.txt"
#define malloc(size)        LeakDetector::xmalloc (size, __FILE__, __LINE__)
#define calloc(count, size) LeakDetector::xcalloc (count, size, __FILE__, __LINE__)
#define free(mem_ref)       LeakDetector::xfree (mem_ref)
#define report_leak         LeakDetector::report_memleakage

#endif // _LEAK_DETECTOR_H_INC_
