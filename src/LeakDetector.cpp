#include "LeakDetector.h"

#undef malloc
#undef calloc
#undef free

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "Platform.h"

namespace LeakDetector {

    namespace {

        // Node of Memory Leak Info
        typedef struct LEAK_INFO
        {
            // Memory Allocation Info
            struct MEM_INFO
            {
                void     *address;
                uint32_t  size;
                char      fn[LEN_FILENAME];
                uint32_t  line_no;

            } mem_info;

            LEAK_INFO *next;

        } LEAK_INFO;


        LEAK_INFO *ptr_head = NULL;
        LEAK_INFO *ptr_curr = NULL;

        // Makes and appends the allocated memory info to the list
        void append_mem_info (void *mem_ref, uint32_t size, const char fn[], uint32_t line_no)
        {
            // append the above info to the list
            LEAK_INFO *ptr_new = (LEAK_INFO *) std::malloc (sizeof (LEAK_INFO));
            if (ptr_new)
            {
                ptr_new->mem_info.address   = mem_ref;
                ptr_new->mem_info.size      = size;
                strncpy_s (ptr_new->mem_info.fn, LEN_FILENAME, fn, LEN_FILENAME);
                ptr_new->mem_info.line_no   = line_no;
                ptr_new->next = NULL;

                if (ptr_curr)
                {
                    ptr_curr->next = ptr_new;
                    ptr_curr       = ptr_curr->next;
                }
                else
                {
                    ptr_curr = ptr_head = ptr_new;
                }
            }
        }
        // Removes the allocated memory info if is part of the list
        void remove_mem_info (void *mem_ref)
        {
            LEAK_INFO *ptr_old = NULL;
            LEAK_INFO *ptr_now = ptr_head;
            // check if allocate memory is in list
            while (ptr_now)
            {
                if (ptr_now->mem_info.address == mem_ref)
                {
                    if (ptr_old)
                    {
                        ptr_old->next = ptr_now->next;
                        std::free (ptr_now);
                    }
                    else
                    {
                        LEAK_INFO *ptr_tmp = ptr_head;
                        ptr_head = ptr_head->next;
                        std::free (ptr_tmp);
                    }

                    return;
                }

                ptr_old = ptr_now;
                ptr_now = ptr_now->next;
            }
        }
        // Clears all the allocated memory info from the list
        void clear_mem_info ()
        {
            ptr_curr = ptr_head;
            while (ptr_curr)
            {
                LEAK_INFO *ptr_tmp = ptr_curr;
                ptr_curr = ptr_curr->next;
                std::free (ptr_tmp);
            }
        }

    }

    // Replacement of malloc
    void* xmalloc (uint32_t size, const char fn[], uint32_t line_no)
    {
        void *mem_ref = std::malloc (size);
        if (mem_ref)
        {
            append_mem_info (mem_ref, size, fn, line_no);
        }
        return mem_ref;
    }
    // Replacement of calloc
    void* xcalloc (uint32_t count, uint32_t size_elem, const char fn[], uint32_t line_no)
    {
        void *mem_ref = std::calloc (count, size_elem);
        if (mem_ref)
        {
            uint32_t size = count * size_elem;
            append_mem_info (mem_ref, size, fn, line_no);
        }
        return mem_ref;
    }
    // Replacement of free
    void  xfree (void *mem_ref)
    {
        remove_mem_info (mem_ref);
        std::free (mem_ref);
    }

    // Writes all info of the unallocated memory into a output file
    void report_memleakage ()
    {
        FILE *fp_write = fopen (FILE_OUTPUT, "wb");
        //errno_t err = fopen_s (&fp_write, FILE_OUTPUT, "wb");

        if (fp_write)
        {
            const uint32_t size = 1024;
            char info_buf[size];
            LEAK_INFO *leak_info;
            leak_info = ptr_head;

            uint32_t x;
            x = sprintf (info_buf, "%s\n", "Memory Leak Summary");
            //x = sprintf_s (info_buf, size, "%s\n", "Memory Leak Summary");
            fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
            x = sprintf (info_buf, "%s\n", "-----------------------------------");
            //x = sprintf_s (info_buf, size, "%s\n", "-----------------------------------");
            fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);

            while (leak_info)
            {
                x = sprintf (info_buf, "address : %p\n", leak_info->mem_info.address);
                //x = sprintf_s (info_buf, size, "address : %p\n", leak_info->mem_info.address);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "size mem: %u bytes\n", leak_info->mem_info.size);
                //x = sprintf_s (info_buf, size, "size mem: %"PR_SIZET"u bytes\n", leak_info->mem_info.size);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "filename: %s\n", leak_info->mem_info.fn);
                //x = sprintf_s (info_buf, size, "filename: %s\n", leak_info->mem_info.fn);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "line #  : %u\n", leak_info->mem_info.line_no);
                //x = sprintf_s (info_buf, size, "line #  : %u\n", leak_info->mem_info.line_no);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "%s\n", "-----------------------------------");
                //x = sprintf_s (info_buf, size, "%s\n", "-----------------------------------");
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);

                leak_info = leak_info->next;
            }
            fclose (fp_write);
        }

        clear_mem_info ();
    }

}
