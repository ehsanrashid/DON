#ifdef LPAGES

#include "MemoryHandler.h"

#include "UCI.h"
#include "Engine.h"
#include <cstdlib>

#if defined(_WIN32)

#   include <tchar.h>
#   include <cstdio>

// disable macros min() and max()
#   ifndef  NOMINMAX
#       define NOMINMAX
#   endif
#   ifndef  WIN32_LEAN_AND_MEAN
#       define WIN32_LEAN_AND_MEAN
#   endif

#   include <windows.h>

#   undef NOMINMAX
#   undef WIN32_LEAN_AND_MEAN

#   define SE_PRIVILEGE_DISABLED       (0x00000000L)

#   define ALIGN_MALLOC(mem, alignment, size) mem=_aligned_malloc (size, alignment)
#   define ALIGN_FREE(mem)                        _aligned_free (mem)

#else    // Linux - Unix

#   include <sys/ipc.h>
#   include <sys/shm.h>
#   include <sys/mman.h>

#   ifndef SHM_HUGETLB
#       define SHM_HUGETLB     04000
#   endif

#   define ALIGN_MALLOC(mem, alignment, size) posix_memalign (&mem, alignment, size)
#   define ALIGN_FREE(mem)                    free (mem)

#endif


namespace Memory {

    using namespace std;

    namespace {

        bool UsePages = false;

#   if defined(_WIN32)

        //void show_error (const char *api_name, DWORD error_code)
        //{
        //    LPSTR msg_buffer_lp = NULL;
        //
        //    FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
        //                     FORMAT_MESSAGE_FROM_SYSTEM |
        //                     FORMAT_MESSAGE_IGNORE_INSERTS,
        //                     NULL, error_code,
        //                     MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
        //                     msg_buffer_lp, 0, NULL);
        //
        //    //... now display this string
        //    _tprintf (TEXT ("ERROR: API        = %s.\n") , api_name);
        //    _tprintf (TEXT ("       error code = %lu.\n"), error_code);
        //    _tprintf (TEXT ("       message    = %s.\n") , msg_buffer_lp);
        //
        //    // Free the buffer allocated by the system
        //    LocalFree (msg_buffer_lp);
        //
        //    error_code = GetLastError ();
        //}

        void setup_privilege (const char *privilege_name, bool enable)
        {
            HANDLE token_handle;
            // Open process token
            if (!OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token_handle))
            {
                //show_error (TEXT ("OpenProcessToken"), GetLastError ());
            }
            
            TOKEN_PRIVILEGES token_priv;
            // Get the luid
            if (!LookupPrivilegeValue (NULL, privilege_name, &token_priv.Privileges[0].Luid))
            {
                //show_error (TEXT ("LookupPrivilegeValue"), GetLastError ());
            }

            token_priv.PrivilegeCount = 1;
            // Enable or Disable privilege
            token_priv.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
            //BOOL status = 
            AdjustTokenPrivileges (token_handle, FALSE, &token_priv, 0, PTOKEN_PRIVILEGES(NULL), 0);

            // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
            // So always check for the last error_code value.
            //DWORD error_code = GetLastError ();
            //if (!status || error_code != ERROR_SUCCESS)
            //{
            //    show_error (TEXT ("AdjustTokenPrivileges"), GetLastError ());
            //}

            // Close the handle
            if (!CloseHandle (token_handle))
            {
                //show_error (TEXT ("CloseHandle"), GetLastError ());
            }
        }

#   else    // Linux - Unix

        i32 shm;

#   endif

    }

    void create_memory (void *&mem_ref, size_t mem_size, size_t alignment)
    {
        UsePages = false;

        if (bool(Options["Large Pages"]))
        {
#   if defined(_WIN32)

            mem_ref = VirtualAlloc
                (NULL,                                  // System selects address
                 mem_size,                              // Size of allocation
                 MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                 PAGE_READWRITE);                       // Protection of Allocation

            if (mem_ref != NULL)
            {
                UsePages = true;
                sync_cout << "info string LargePage Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }

            mem_ref = VirtualAlloc
                (NULL,                 // System selects address
                mem_size,              // Size of allocation
                MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                PAGE_READWRITE);       // Protection of Allocation

            if (mem_ref != NULL)
            {
                UsePages = true;
                memset (mem_ref, 0x00, mem_size);
                sync_cout << "info string Page Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }
            cerr << "ERROR: VirtualAlloc() virtual memory alloc failed.";

#   else    // Linux - Unix

            shm = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (shm >= 0)
            {
                mem_ref = shmat (shm, NULL, 0x0);
                if (mem_ref != (char*) -1)
                {
                    UsePages = true;
                    memset (mem_ref, 0x00, mem_size);
                    sync_cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB." << sync_endl;
                    return;
                }
                cerr << "ERROR: shmat() shared memory attach failed.";
                if (shmctl (shm, IPC_RMID, NULL) == -1)
                {
                    cerr << "ERROR: shmctl(IPC_RMID) failed.";
                }
                return;
            }
            cerr << "ERROR: shmget() shared memory alloc failed.";

#   endif
        }

        ALIGN_MALLOC (mem_ref, alignment, mem_size);
        if (mem_ref != NULL)
        {
            memset (mem_ref, 0x00, mem_size);
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;
            return;
        }

        cerr << "ERROR: Hash allocate failed " << (mem_size >> 20) << " MB." << endl;
    }

    void   free_memory (void *mem)
    {
        if (mem == NULL) return;

        if (UsePages)
        {
#   if defined(_WIN32)
            
            if (VirtualFree (mem, 0, MEM_RELEASE))
            {
            }

#   else   // Linux - Unix

            if (shmdt (mem) == -1)
            {
                cerr << "ERROR: shmdt() shared memory detach failed." << endl;
            }
            if (shmctl (shm, IPC_RMID, NULL) == -1)
            {
                cerr << "ERROR: shmctl(IPC_RMID) failed.";
            }
#   endif
            UsePages = false;
            return;
        }

        ALIGN_FREE (mem);
    }
    
    void initialize    ()
    {

#   if defined(_WIN32)

        setup_privilege (SE_LOCK_MEMORY_NAME, true);
        
#   else    // Linux - Unix

#   endif

    }

}

#endif // LPAGES
