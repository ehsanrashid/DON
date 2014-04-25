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

#   define MEMALIGN(mem, align, size)  mem = _aligned_malloc (size, align)
#   define ALIGNED_FREE(mem)           _aligned_free (mem);

#   define SE_PRIVILEGE_DISABLED       (0x00000000L)

#else    // Linux - Unix

#   include <sys/ipc.h>
#   include <sys/shm.h>

#endif

namespace MemoryHandler {

    using namespace std;

    namespace {

        bool UsePages = false;

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        //VOID error_exit (const LPSTR lpAPI, DWORD dwError)
        //{
        //    LPSTR lpvMessageBuffer = NULL;
        //
        //    FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
        //                     FORMAT_MESSAGE_FROM_SYSTEM |
        //                     FORMAT_MESSAGE_IGNORE_INSERTS,
        //                     NULL, dwError,
        //                     MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
        //                     lpvMessageBuffer, 0, NULL);
        //
        //    //... now display this string
        //    _tprintf (TEXT ("ERROR: API        = %s.\n"), lpAPI);
        //    _tprintf (TEXT ("       error code = %lu.\n"), dwError);
        //    _tprintf (TEXT ("       message    = %s.\n"), lpvMessageBuffer);
        //
        //    // Free the buffer allocated by the system
        //    LocalFree (lpvMessageBuffer);
        //
        //    dwError = GetLastError ();
        //    Engine::exit (dwError);
        //}

        VOID setup_privilege (const LPSTR lpPrivilege, BOOL bEnable)
        {
            HANDLE hToken;
            // Open process token
            if (!OpenProcessToken (GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken))
            {
                //error_exit (TEXT (const_cast<LPSTR> ("OpenProcessToken")), GetLastError ());
            }
            
            TOKEN_PRIVILEGES tp;
            // Enable or Disable privilege
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = (bEnable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
            // Get the luid
            if (!LookupPrivilegeValue (NULL, lpPrivilege, &tp.Privileges[0].Luid))
            {
                //error_exit (TEXT (const_cast<LPSTR> ("LookupPrivilegeValue")), GetLastError ());
            }

            //BOOL bStatus = 
            AdjustTokenPrivileges (hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES) NULL, 0);

            // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
            // So always check for the last dwError value.
            //DWORD dwError = GetLastError ();
            //if (!bStatus || (dwError != ERROR_SUCCESS))
            //{
            //    error_exit (TEXT (const_cast<LPSTR> ("AdjustTokenPrivileges")), GetLastError ());
            //}

            // Close the handle
            if (!CloseHandle (hToken))
            {
                //error_exit (TEXT (const_cast<LPSTR> ("CloseHandle")), GetLastError ());
            }
        }

#   else    // Linux - Unix

        i32 Num;

#   endif

    }

    void create_memory  (void *&mem_ref, u64 mem_size, u08 align)
    {
        UsePages = false;

        if (bool (Options["Large Pages"]))
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
                cout << "info string LargePage Hash " << (mem_size >> 20) << " MB." << endl;
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
                cout << "info string Page Hash " << (mem_size >> 20) << " MB." << endl;
                return;
            }

#   else    // Linux - Unix

            Num = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (Num >= 0)
            {
                mem_ref = shmat (Num, NULL, 0x0);
                if (mem_ref != -1)
                {
                    UsePages = true;
                    memset (mem_ref, 0x00, SHMSZ);
                    cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB." << endl;
                    return;
                }
                //perror ("shmat: Shared memory attach failure");
                //shmctl (shmid1, IPC_RMID, NULL);
                //Engine::exit (EXIT_FAILURE);
            }
            else
            {
                //perror ("shmget: Shared memory get failure");
                //Engine::exit (EXIT_FAILURE);
            }
#   endif
        }

        MEMALIGN (mem_ref, align, mem_size);
        if (mem_ref != NULL)
        {
            memset (mem_ref, 0x00, mem_size);
            cout << "info string Hash " << (mem_size >> 20) << " MB." << endl;
            return;
        }

        cerr << "ERROR: Failed to allocate Hash" << (mem_size >> 20) << " MB." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    void   free_memory  (void *mem)
    {
        if (mem == NULL) return;

        if (UsePages)
        {
#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
            
            if (VirtualFree (mem, 0, MEM_RELEASE))
            {
                ;
            }

#   else   // Linux - Unix

            if (shmdt (mem) != 0) { cerr << "Could not close memory segment." << endl; }
            shmctl (Num, IPC_RMID, NULL);
            
#   endif
            UsePages = false;
            return;
        }

        ALIGNED_FREE (mem);
    }
    
    void initialize     ()
    {
#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        setup_privilege (TEXT (const_cast<LPSTR> ("SeLockMemoryPrivilege")), TRUE);
        
#   else    // Linux - Unix

#   endif
    }

}

#endif // LPAGES