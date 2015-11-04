#ifdef LPAGES

#include "MemoryHandler.h"

#include <cstdlib>
#include "UCI.h"
#include "Thread.h"
#include "Engine.h"

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

#   define ALIGN_MALLOC(mem, alignment, size)   mem=_aligned_malloc (size, alignment)
#   define ALIGN_FREE(mem)                          _aligned_free (mem)

#else

#   include <sys/ipc.h>
#   include <sys/shm.h>
#   include <sys/mman.h>

#   ifndef SHM_HUGETLB
#       define SHM_HUGETLB     04000
#   endif

#   define ALIGN_MALLOC(mem, alignment, size)   posix_memalign (&mem, alignment, size)
#   define ALIGN_FREE(mem)                      free (mem)

#endif


namespace Memory {

    using namespace std;

    namespace {

        bool LargePages = false;

#   if defined(_WIN32)

        //void show_error (const char *api_name, DWORD error_code)
        //{
        //    LPSTR msg_buffer_lp = nullptr;
        //
        //    FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
        //                     FORMAT_MESSAGE_FROM_SYSTEM |
        //                     FORMAT_MESSAGE_IGNORE_INSERTS,
        //                     nullptr, error_code,
        //                     MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
        //                     msg_buffer_lp, 0, nullptr);
        //
        //    //... now display this string
        //    _tprintf (TEXT ("ERROR: API        = %s.\n") , api_name);
        //    _tprintf (TEXT ("       Error code = %lu.\n"), error_code);
        //    _tprintf (TEXT ("       Message    = %s.\n") , msg_buffer_lp);
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
            if (!LookupPrivilegeValue (nullptr, privilege_name, &token_priv.Privileges[0].Luid))
            {
                //show_error (TEXT ("LookupPrivilegeValue"), GetLastError ());
            }

            token_priv.PrivilegeCount = 1;
            // Enable or Disable privilege
            token_priv.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
            //BOOL status = 
            AdjustTokenPrivileges (token_handle, FALSE, &token_priv, 0, PTOKEN_PRIVILEGES(nullptr), 0);

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

#   else

        i32 shm; // Shared Memory Identifier

#   endif

    }

    void alloc_memory (void *&mem_ref, u64 mem_size, u32 alignment)
    {
        LargePages = false;

        if (bool(Options["Large Pages"]))
        {
#   if defined(_WIN32)

            mem_ref = VirtualAlloc
                (nullptr,                               // System selects address
                 mem_size,                              // Size of allocation
                 MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                 PAGE_READWRITE);                       // Protection of Allocation

            if (mem_ref != nullptr)
            {
                LargePages = true;
                sync_cout << "info string LargePage Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }

            mem_ref = VirtualAlloc
                (nullptr,              // System selects address
                mem_size,              // Size of allocation
                MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                PAGE_READWRITE);       // Protection of Allocation

            if (mem_ref != nullptr)
            {
                LargePages = true;
                memset (mem_ref, 0x00, mem_size);
                sync_cout << "info string Page Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }
            std::cerr << "ERROR: VirtualAlloc() virtual memory alloc failed." << std::endl;

#   else

            shm = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (shm != -1)
            {
                mem_ref = shmat (shm, nullptr, 0x00);
                if (mem_ref != (void*) -1)
                {
                    LargePages = true;
                    memset (mem_ref, 0x00, mem_size);
                    sync_cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB." << sync_endl;
                    return;
                }
                std::cerr << "ERROR: shmat() shared memory attach failed." << std::endl;
                if (shmctl (shm, IPC_RMID, nullptr) == -1)
                {
                    std::cerr << "ERROR: shmctl(IPC_RMID) failed." << std::endl;
                }
                return;
            }
            shm = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W);
            if (shm != -1)
            {
                mem_ref = shmat (shm, nullptr, 0x00);
                if (mem_ref != (void*) -1)
                {
                    LargePages = true;
                    memset (mem_ref, 0x00, mem_size);
                    sync_cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB." << sync_endl;
                    return;
                }
                std::cerr << "ERROR: shmat() shared memory attach failed." << std::endl;
                if (shmctl (shm, IPC_RMID, nullptr) == -1)
                {
                    std::cerr << "ERROR: shmctl(IPC_RMID) failed." << std::endl;
                }
                return;
            }
            std::cerr << "ERROR: shmget() shared memory alloc failed." << std::endl;

#   endif
        }

        ALIGN_MALLOC (mem_ref, alignment, mem_size);
        if (mem_ref != nullptr)
        {
            memset (mem_ref, 0x00, mem_size);
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;
            return;
        }

        std::cerr << "ERROR: Hash allocate failed " << (mem_size >> 20) << " MB." << std::endl;
    }

    void  free_memory (void *mem)
    {
        if (mem == nullptr) return;

        if (LargePages)
        {
#   if defined(_WIN32)
            
            if (VirtualFree (mem, 0, MEM_RELEASE))
            {
            }

#   else

            if (shmdt (mem) == -1)
            {
                std::cerr << "ERROR: shmdt() shared memory detach failed." << std::endl;
            }
            if (shmctl (shm, IPC_RMID, nullptr) == -1)
            {
                std::cerr << "ERROR: shmctl(IPC_RMID) failed." << std::endl;
            }
#   endif
            LargePages = false;
            return;
        }

        ALIGN_FREE (mem);
    }

    void initialize   ()
    {
#   if defined(_WIN32)

        setup_privilege (SE_LOCK_MEMORY_NAME, true);

#   else

#   endif
    }

}

#endif // LPAGES
