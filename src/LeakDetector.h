//#pragma once
#ifndef LEAK_DETECTOR_H_
#define LEAK_DETECTOR_H_

namespace LeakDetector {

    typedef unsigned int    uint;

    extern void* xmalloc (size_t size, const char fn[], uint line_no);
    extern void* xcalloc (size_t count, size_t size_elem, const char fn[], uint line_no);
    extern void  xfree (void *mem_ref);

    extern void report_memleakage ();

}

#define LEN_FILENAME        256
#define FILE_OUTPUT         "info_leak.txt"
#define malloc(size)        LeakDetector::xmalloc(size, __FILE__, __LINE__)
#define calloc(count, size) LeakDetector::xcalloc(count, size, __FILE__, __LINE__)
#define free(mem_ref)       LeakDetector::xfree(mem_ref)
#define report_leak()       LeakDetector::report_memleakage()

#endif
