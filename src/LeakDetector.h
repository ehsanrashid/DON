//#pragma once
#ifndef LEAK_DETECTOR_H_
#define LEAK_DETECTOR_H_

#include "Type.h"

namespace LeakDetector {

    extern void* xmalloc (uint32_t size, const char fn[], uint32_t line_no);
    extern void* xcalloc (uint32_t count, uint32_t size_elem, const char fn[], uint32_t line_no);
    extern void  xfree (void *mem_ref);

    extern void report_memleakage ();

}

#define LEN_FILENAME        256
#define FILE_OUTPUT         "leak_info.txt"
#define malloc(size)        LeakDetector::xmalloc (size, __FILE__, __LINE__)
#define calloc(count, size) LeakDetector::xcalloc (count, size, __FILE__, __LINE__)
#define free(mem_ref)       LeakDetector::xfree (mem_ref)
#define report_leak         LeakDetector::report_memleakage

#endif
