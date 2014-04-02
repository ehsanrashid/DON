#ifdef LPAGES

#include "MemoryHandler.h"

#include "UCI.h"
#include "Engine.h"

#if defined(_WIN32) || defined(_MSC_VER) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

#   include <tchar.h>
#   include <stdio.h>
#   include <stdlib.h>

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

        /// http://msdn.microsoft.com/en-us/library/aa366543%28VS.85%29.aspx
        
        typedef INT (*GetLargePageMinimum) (VOID);

        VOID ErrorExit (const LPSTR lpAPI, DWORD dwError)
        {
            LPSTR lpvMessageBuffer = NULL;

            FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                             FORMAT_MESSAGE_FROM_SYSTEM |
                             FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, dwError,
                             MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
                             lpvMessageBuffer, 0, NULL);

            //... now display this string
            _tprintf (TEXT ("ERROR: API        = %s.\n"), lpAPI);
            _tprintf (TEXT ("       error code = %ld.\n"), dwError);
            _tprintf (TEXT ("       message    = %s.\n"), lpvMessageBuffer);

            // Free the buffer allocated by the system
            LocalFree (lpvMessageBuffer);

            dwError = GetLastError ();
            ExitProcess  (dwError);
        }
        
        VOID SetupPrivilege (const LPSTR lpPrivilege, BOOL bEnable)
        {
            HANDLE hProcess = GetCurrentProcess();
            HANDLE hToken;
            // Open process token
            if (!OpenProcessToken (hProcess, TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken))
            {
                //ErrorExit (TEXT (const_cast<LPSTR> ("OpenProcessToken")), GetLastError ());
            }
            
            TOKEN_PRIVILEGES tp;
            
            // Enable or Disable privilege
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = (bEnable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
            // Get the luid
            if (!LookupPrivilegeValue (NULL, lpPrivilege, &tp.Privileges[0].Luid))
            {
                //ErrorExit (TEXT (const_cast<LPSTR> ("LookupPrivilegeValue")), GetLastError ());
            }

            BOOL bStatus = AdjustTokenPrivileges (hToken, FALSE, &tp, 0, (PTOKEN_PRIVILEGES) NULL, 0);

            // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
            // So always check for the last dwError value.
            DWORD dwError = GetLastError ();
            if (!bStatus || (dwError != ERROR_SUCCESS))
            {
                //ErrorExit (TEXT (const_cast<LPSTR> ("AdjustTokenPrivileges")), GetLastError ());
            }

            // Close the handle
            if (!CloseHandle (hToken))
            {
                //ErrorExit (TEXT (const_cast<LPSTR> ("CloseHandle")), GetLastError ());
            }
        }

        /*

        //#       define PAGELIMIT    80            // Number of pages to ask for

        //LPTSTR lpNxtPage;               // Address of the next page to ask for
        //DWORD dwPages = 0;              // Count of pages gotten so far
        //DWORD dwPageSize;               // Page size on this computer

        //INT PageFaultExceptionFilter (DWORD dwCode)
        //{
        //    LPVOID lpvResult;

        //    // If the exception is not a page fault, exit.

        //    if (dwCode != EXCEPTION_ACCESS_VIOLATION)
        //    {
        //        _tprintf (TEXT ("Exception code = %ld.\n"), dwCode);
        //        return EXCEPTION_EXECUTE_HANDLER;
        //    }

        //    _tprintf (TEXT ("Exception is a page fault.\n"));

        //    // If the reserved pages are used up, exit.

        //    if (dwPages >= PAGELIMIT)
        //    {
        //        _tprintf (TEXT ("Exception: out of pages.\n"));
        //        return EXCEPTION_EXECUTE_HANDLER;
        //    }

        //    // Otherwise, commit another page.

        //    lpvResult = VirtualAlloc (
        //                     (LPVOID) lpNxtPage, // Next page to commit
        //                     dwPageSize,         // Page size, in bytes
        //                     MEM_COMMIT,         // Allocate a committed page
        //                     PAGE_READWRITE);    // Read/write access
        //    if (lpvResult == NULL)
        //    {
        //        _tprintf (TEXT ("VirtualAlloc failed.\n"));
        //        return EXCEPTION_EXECUTE_HANDLER;
        //    }
        //    else
        //    {
        //        _tprintf (TEXT ("Allocating another page.\n"));
        //    }

        //    // Increment the page count, and advance lpNxtPage to the next page.

        //    ++dwPages;
        //    lpNxtPage = (LPTSTR) ((PCHAR) lpNxtPage + dwPageSize);

        //    // Continue execution where the page fault occurred.

        //    return EXCEPTION_CONTINUE_EXECUTION;
        //}
        */

#   else    // Linux - Unix

        i32 Num;

#   endif

    }

    void create_memory  (void *&mem_ref, u64 mem_size, u08 align)
    {
        UsePages = false;

        if (bool (*(Options["Large Pages"])))
        {

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

            /* Vlad0 */
            mem_ref = VirtualAlloc
                (NULL,                      // System selects address
                 mem_size,                  // Size of allocation
                 MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE, // Type of Allocation
                 PAGE_READWRITE);           // Protection of Allocation

            if (mem_ref != NULL)
            {
                UsePages = true;
                cout << "info string LargePage Hash " << (mem_size >> 20) << " MB..." << endl;
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
                cout << "info string Page Hash " << (mem_size >> 20) << " MB..." << endl;
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
                    memset (mem_ref, 0, SHMSZ);
                    cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB..." << endl;
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
            memset (mem_ref, 0, mem_size);
            cout << "info string Hash " << (mem_size >> 20) << " MB..." << endl;
            return;
        }

        cerr << "ERROR: Failed to allocate Hash" << (mem_size >> 20) << " MB..." << endl;
        Engine::exit (EXIT_FAILURE);
    }

    void free_memory    (void *mem)
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

            if(shmdt  (mem) != 0) { cerr << "Could not close memory segment." << endl; }
            shmctl (Num, IPC_RMID, NULL);
            
#   endif
            UsePages = false;
            return;
        }

        ALIGNED_FREE (mem);
        
    }

    void initialize      ()
    {

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

        SetupPrivilege (TEXT (const_cast<LPSTR> ("SeLockMemoryPrivilege")), TRUE);
        
        /*
        
        //#       define BUF_SIZE     65536

        //// Call succeeds only on Windows Server 2003 SP1 or later
        //HINSTANCE hDll = LoadLibrary (TEXT ("kernel32.dll"));
        //if (hDll == NULL)
        //{
        //    ErrorExit (TEXT (const_cast<LPSTR> ("LoadLibrary")), GetLastError ());
        //}

        //GetLargePageMinimum pGetLargePageMinimum =
        //    GetLargePageMinimum (GetProcAddress (hDll, "GetLargePageMinimum"));
        //if (pGetLargePageMinimum == NULL)
        //{
        //    ErrorExit (TEXT (const_cast<LPSTR> ("GetProcAddress")), GetLastError ());
        //}

        //DWORD dwSize = pGetLargePageMinimum ();

        //FreeLibrary (hDll);

        //_tprintf (TEXT ("Page Size: %ld\n"), dwSize);

        //SetupPrivilege (TEXT (const_cast<LPSTR> ("SeLockMemoryPrivilege")), TRUE);
        //
        //TCHAR szName[] = TEXT ("LARGEPAGE");
        //HANDLE hMapFile = CreateFileMapping (
        //     INVALID_HANDLE_VALUE,    // use paging file
        //     NULL,                    // default security
        //     //PAGE_READWRITE|SEC_COMMIT|SEC_LARGE_PAGES,
        //     PAGE_READWRITE|MEM_LARGE_PAGES,
        //     0,                       // max. object size
        //     dwSize,                  // buffer size
        //     szName);                 // name of mapping object

        //if (hMapFile == NULL)
        //{
        //    ErrorExit (TEXT (const_cast<LPSTR> ("CreateFileMapping")), GetLastError ());
        //}
        //else
        //{
        //    _tprintf (TEXT ("File mapping object successfulyl created.\n"));
        //}

        //SetupPrivilege (TEXT (const_cast<LPSTR> ("SeLockMemoryPrivilege")), FALSE);

        //LPCTSTR pBuf = (LPTSTR) MapViewOfFile (hMapFile,   // handle to map object
        //     FILE_MAP_ALL_ACCESS, // read/write permission
        //     0,
        //     0,
        //     BUF_SIZE);

        //if (pBuf == NULL)
        //{
        //    ErrorExit (TEXT (const_cast<LPSTR> ("MapViewOfFile")), GetLastError ());
        //}
        //else
        //{
        //    _tprintf (TEXT ("View of file successfully mapped.\n"));
        //}

        //// do nothing, clean up an exit
        //UnmapViewOfFile (pBuf);
        //CloseHandle (hMapFile);
        */

        /*
        //SYSTEM_INFO sSysInfo;         // Useful information about the system

        //GetSystemInfo(&sSysInfo);     // Initialize the structure.

        //_tprintf (TEXT ("This computer has page size %d.\n"), sSysInfo.dwPageSize);

        //dwPageSize = sSysInfo.dwPageSize;

        //// Reserve pages in the virtual address space of the process.
        //// Base address of the test memory
        //LPVOID lpvBase = VirtualAlloc (
        //                NULL,                   // System selects address
        //                PAGELIMIT*dwPageSize,   // Size of allocation
        //                MEM_COMMIT|MEM_RESERVE, // Allocate reserved pages
        //                //PAGE_NOACCESS);       // Protection = no access
        //                PAGE_READWRITE);

        //if (lpvBase == NULL)
        //{
        //    ErrorExit (TEXT (const_cast<LPSTR> ("VirtualAlloc reserve failed.")), GetLastError ());
        //}

        //// Generic character pointer
        //LPTSTR lpPtr = lpNxtPage = (LPTSTR) lpvBase;

        //// Use structured exception handling when accessing the pages.
        //// If a page fault occurs, the exception filter is executed to
        //// commit another page from the reserved block of pages.

        //for (DWORD i=0; i < PAGELIMIT*dwPageSize; ++i)
        //{
        //    __try
        //    {
        //        // Write to memory.

        //        lpPtr[i] = 'a';
        //    }
        //    // If there's a page fault, commit another page and try again.
        //    __except (PageFaultExceptionFilter (GetExceptionCode ()))
        //    {
        //        // This code is executed only if the filter function
        //        // is unsuccessful in committing the next page.
        //        _tprintf (TEXT ("Exiting process.\n"));

        //        ExitProcess (GetLastError ());
        //    }
        //}

        //// Release the block of pages when you are finished using them.

        //BOOL bSuccess = VirtualFree (
        //                   lpvBase,       // Base address of block
        //                   0,             // Bytes of committed pages
        //                   MEM_RELEASE);  // Decommit the pages

        //_tprintf (TEXT ("Release %s.\n"), bSuccess ? TEXT ("succeeded") : TEXT ("failed"));
        */

#   else    // Linux - Unix



#   endif

    }

}

#endif