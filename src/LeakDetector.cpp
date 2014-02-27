#include "LeakDetector.h"

#undef malloc
#undef calloc
#undef free

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include "Platform.h"

#   if defined(_MSC_VER)
#       pragma warning (disable: 4996) // 'argument': This function or variable may be unsafe.
#endif

namespace LeakDetector {

    using namespace std;

    namespace {

        // Node of Memory Leak Info
        typedef struct LEAK_INFO
        {
            // Memory Allocation Info
            struct MEM_INFO
            {
                void     *address;
                size_t    size;
                char      filename[LEN_FILENAME];
                uint32_t  line_no;

            } mem_info;

            LEAK_INFO *next;

        } LEAK_INFO;


        LEAK_INFO *head_ptr = NULL;
        LEAK_INFO *curr_ptr = NULL;

        // Makes and appends the allocated memory info to the list
        void append_mem_info (void *mem_ref, size_t size, const char filename[], uint32_t line_no)
        {
            // append the above info to the list
            LEAK_INFO *new_ptr = (LEAK_INFO *) malloc (sizeof (LEAK_INFO));
            if (new_ptr)
            {
                new_ptr->mem_info.address   = mem_ref;
                new_ptr->mem_info.size      = size;
                strncpy_s (new_ptr->mem_info.filename, LEN_FILENAME, filename, LEN_FILENAME);
                new_ptr->mem_info.line_no   = line_no;
                new_ptr->next = NULL;

                if (curr_ptr)
                {
                    curr_ptr->next = new_ptr;
                    curr_ptr       = curr_ptr->next;
                }
                else
                {
                    curr_ptr = head_ptr = new_ptr;
                }
            }
        }
        // Removes the allocated memory info if is part of the list
        void remove_mem_info (void *mem_ref)
        {
            LEAK_INFO *old_ptr = NULL;
            LEAK_INFO *itr_ptr = head_ptr;
            // check if allocate memory is in list
            while (itr_ptr)
            {
                if (itr_ptr->mem_info.address == mem_ref)
                {
                    if (old_ptr)
                    {
                        old_ptr->next = itr_ptr->next;
                        free (itr_ptr);
                    }
                    else
                    {
                        LEAK_INFO *tmp_ptr = head_ptr;
                        head_ptr = head_ptr->next;
                        free (tmp_ptr);
                    }

                    return;
                }

                old_ptr = itr_ptr;
                itr_ptr = itr_ptr->next;
            }
        }
        // Clears all the allocated memory info from the list
        void clear_mem_info ()
        {
            curr_ptr = head_ptr;
            while (curr_ptr)
            {
                LEAK_INFO *tmp_ptr = curr_ptr;
                curr_ptr = curr_ptr->next;
                free (tmp_ptr);
            }
        }

    }

    // Replacement of malloc
    void* xmalloc (size_t size, const char filename[], uint32_t line_no)
    {
        void *mem_ref = malloc (size);
        if (mem_ref)
        {
            append_mem_info (mem_ref, size, filename, line_no);
        }
        return mem_ref;
    }
    // Replacement of calloc
    void* xcalloc (size_t count, size_t size, const char filename[], uint32_t line_no)
    {
        void *mem_ref = calloc (count, size);
        if (mem_ref)
        {
            append_mem_info (mem_ref, count * size, filename, line_no);
        }
        return mem_ref;
    }
    // Replacement of free
    void  xfree (void *mem_ref)
    {
        remove_mem_info (mem_ref);
        free (mem_ref);
    }

    // Writes all info of the unallocated memory into a output file
    void report_memleakage ()
    {
        FILE *fp_write = fopen (FILE_OUTPUT, "wb");
        //errno_t err = fopen_s (&fp_write, FILE_OUTPUT, "wb");

        if (fp_write)
        {
#           define BUF_SIZE 1024
            char info_buf[BUF_SIZE];
            LEAK_INFO *leak_info;
            leak_info = head_ptr;

            int32_t x;
            x = sprintf (info_buf, "%s\n", "Memory Leak Summary");
            //x = sprintf_s (info_buf, BUF_SIZE, "%s\n", "Memory Leak Summary");
            fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
            x = sprintf (info_buf, "%s\n", "-----------------------------------");
            //x = sprintf_s (info_buf, BUF_SIZE, "%s\n", "-----------------------------------");
            fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);

            while (leak_info)
            {
                x = sprintf (info_buf, "Address : %p\n", leak_info->mem_info.address);
                //x = sprintf_s (info_buf, BUF_SIZE, "Address : %p\n", leak_info->mem_info.address);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "Size    : %u bytes\n", leak_info->mem_info.size);
                //x = sprintf_s (info_buf, BUF_SIZE, "Size    : %"PR_SIZET"u bytes\n", leak_info->mem_info.size);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "Filename: %s\n", leak_info->mem_info.filename);
                //x = sprintf_s (info_buf, BUF_SIZE, "Filename: %s\n", leak_info->mem_info.filename);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "Line #  : %u\n", leak_info->mem_info.line_no);
                //x = sprintf_s (info_buf, BUF_SIZE, "Line #  : %u\n", leak_info->mem_info.line_no);
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);
                x = sprintf (info_buf, "%s\n", "-----------------------------------");
                //x = sprintf_s (info_buf, BUF_SIZE, "%s\n", "-----------------------------------");
                fwrite (info_buf, strlen (info_buf) + 1, 1, fp_write);

                leak_info = leak_info->next;
            }
            fclose (fp_write);
        }

        clear_mem_info ();
    }

}
