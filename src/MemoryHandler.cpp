#ifdef LPAGES

#include "MemoryHandler.h"

#include "UCI.h"
#include "Engine.h"

#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   include <tchar.h>
#   include <cstdio>
#   include <cstdlib>

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

#   define MEMALIGN(mem, alignment, size)  mem = _aligned_malloc (size, alignment)
#   define ALIGNED_FREE(mem)                _aligned_free (mem);

#   define SE_PRIVILEGE_DISABLED       (0x00000000L)

#else    // Linux - Unix

#   include <sys/ipc.h>
#   include <sys/shm.h>

#endif

namespace Memory {

    using namespace std;

    namespace {

        bool UsePages = false;

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        //void exit_error (const LPSTR api_lp, DWORD error_code)
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
        //    _tprintf (TEXT ("ERROR: API        = %s.\n") , api_lp);
        //    _tprintf (TEXT ("       error code = %lu.\n"), error_code);
        //    _tprintf (TEXT ("       message    = %s.\n") , msg_buffer_lp);
        //
        //    // Free the buffer allocated by the system
        //    LocalFree (msg_buffer_lp);
        //
        //    error_code = GetLastError ();
        //}

        void setup_privilege (const LPSTR privilege_lp, BOOL enable)
        {
            HANDLE token_handle;
            // Open process token
            if (!OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token_handle))
            {
                //exit_error (TEXT (const_cast<LPSTR> ("OpenProcessToken")), GetLastError ());
            }
            
            TOKEN_PRIVILEGES token_priv;
            // Enable or Disable privilege
            token_priv.PrivilegeCount = 1;
            token_priv.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
            // Get the luid
            if (!LookupPrivilegeValue (NULL, privilege_lp, &token_priv.Privileges[0].Luid))
            {
                //exit_error (TEXT (const_cast<LPSTR> ("LookupPrivilegeValue")), GetLastError ());
            }

            //BOOL status = 
            AdjustTokenPrivileges (token_handle, FALSE, &token_priv, 0, static_cast<PTOKEN_PRIVILEGES>(NULL), 0);

            // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
            // So always check for the last error_code value.
            //DWORD error_code = GetLastError ();
            //if (!status || error_code != ERROR_SUCCESS)
            //{
            //    exit_error (TEXT (const_cast<LPSTR> ("AdjustTokenPrivileges")), GetLastError ());
            //}

            // Close the handle
            if (!CloseHandle (token_handle))
            {
                //exit_error (TEXT (const_cast<LPSTR> ("CloseHandle")), GetLastError ());
            }
        }

#   else    // Linux - Unix

        i32 SharedMemoryKey;

#   endif

    }

    void create_memory (void *&mem_ref, size_t mem_size, size_t alignment)
    {
        UsePages = false;

        if (bool(Options["Large Pages"]))
        {
#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

            mem_ref = VirtualAlloc
                (NULL,                      // System selects address
                 mem_size,                  // Size of allocation
                 MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE, // Type of Allocation
                 PAGE_READWRITE);           // Protection of Allocation

            if (mem_ref != NULL)
            {
                UsePages = true;
                sync_cout << "info string LargePage Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }

            mem_ref = VirtualAlloc
                (NULL,                  // System selects address
                mem_size,              // Size of allocation
                MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                PAGE_READWRITE);       // Protection of Allocation

            if (mem_ref != NULL)
            {
                UsePages = true;
                sync_cout << "info string Page Hash " << (mem_size >> 20) << " MB." << sync_endl;
                return;
            }

#   else    // Linux - Unix

            SharedMemoryKey = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (SharedMemoryKey >= 0)
            {
                mem_ref = shmat (SharedMemoryKey, NULL, 0x0);
                if (mem_ref != -1)
                {
                    UsePages = true;
                    memset (mem_ref, 0x00, SHMSZ);
                    sync_cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB." << sync_endl;
                    return;
                }
                //perror ("shmat: Shared memory attach failure");
                //shmctl (shmid1, IPC_RMID, NULL);
                return;
            }
            //perror ("shmget: Shared memory get failure");

#   endif
        }

        MEMALIGN (mem_ref, alignment, mem_size);
        if (mem_ref != NULL)
        {
            memset (mem_ref, 0x00, mem_size);
            sync_cout << "info string Hash " << (mem_size >> 20) << " MB." << sync_endl;
            return;
        }

        cerr << "ERROR: failed to allocate Hash " << (mem_size >> 20) << " MB." << endl;
    }

    void   free_memory (void *mem)
    {
        if (mem == NULL) return;

        if (UsePages)
        {
#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
            
            if (VirtualFree (mem, 0, MEM_RELEASE))
            {
            }

#   else   // Linux - Unix

            if (shmdt (mem)) { cerr << "Could not close memory segment." << endl; }
            shmctl (SharedMemoryKey, IPC_RMID, NULL);
            
#   endif
            UsePages = false;
            return;
        }

        ALIGNED_FREE (mem);
    }
    
    void initialize    ()
    {
#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        setup_privilege (TEXT (const_cast<LPSTR> ("SeLockMemoryPrivilege")), TRUE);
        
#   else    // Linux - Unix

#   endif
    }

}

#endif // LPAGES