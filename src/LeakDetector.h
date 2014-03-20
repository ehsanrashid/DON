#ifdef _MSC_VER
#   pragma once
#endif

#ifndef _LEAK_DETECTOR_H_INC_
#define _LEAK_DETECTOR_H_INC_

#include "Platform.h"

namespace LeakDetector {

    extern void* xmalloc (u64 size, const char filename[], u32 line_no);
    extern void* xcalloc (u64 count, u64 size, const char filename[], u32 line_no);
    extern void  xfree (void *mem_ref);

    extern void report_memleakage ();

}

#define FN_SIZE             256
#define INFO_FN             "LeakInfo.txt"
#define malloc(size)        LeakDetector::xmalloc (size, __FILE__, __LINE__)
#define calloc(count, size) LeakDetector::xcalloc (count, size, __FILE__, __LINE__)
#define free(mem_ref)       LeakDetector::xfree (mem_ref)
#define report_leak         LeakDetector::report_memleakage

#endif // _LEAK_DETECTOR_H_INC_
