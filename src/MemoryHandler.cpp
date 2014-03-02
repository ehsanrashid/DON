#ifdef LPAGES

#include "MemoryHandler.h"

#include "UCI.h"

#if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

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

    namespace {

        bool use_largepages = false;

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
            HANDLE process = GetCurrentProcess();
            HANDLE hToken;
            // Open process token
            if (!OpenProcessToken (process, TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &hToken))
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



#   endif

    }

    void create_memory  (void *&mem_ref, uint64_t mem_size, uint8_t align)
    {
        use_largepages = false;

        if (bool (*(Options["Large Pages"])))
        {

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)

            /* Vlad0 */
            mem_ref = VirtualAlloc (
                        NULL,                                   // System selects address
                        SIZE_T (mem_size),                      // Size of allocation
                        MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE, // Type of Allocation
                        PAGE_READWRITE);                        // Protection of Allocation

            if (mem_ref)
            {
                use_largepages = true;
                std::cout << "info string WindowsLargePages Hash " << (mem_size >> 20) << " MB..." << std::endl;
                return;
            }
            else
            {
                mem_ref = VirtualAlloc (
                        NULL,                   // System selects address
                        SIZE_T (mem_size),      // Size of allocation
                        MEM_COMMIT|MEM_RESERVE, // Type of Allocation
                        PAGE_READWRITE);        // Protection of Allocation
                
                if (mem_ref)
                {
                    use_largepages = true;
                    std::cout << "info string WindowsPages Hash " << (mem_size >> 20) << " MB..." << std::endl;
                    return;
                }
            }

#   else    // Linux - Unix

            int32_t num = shmget (IPC_PRIVATE, mem_size, IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (num >= 0)
            {
                mem_ref = shmat (num, NULL, 0x0);
                if (mem_ref == -1)
                {
                    //perror ("shmat: Shared memory attach failure");
                    //shmctl (shmid1, IPC_RMID, NULL);
                    //exit(2);
                }
                use_largepages = true;
                std::cout << "info string HUGELTB Hash " << (mem_size >> 20) << " MB..." << std::endl;
                return;
            }
            else
            {
                //perror ("shmget: Shared memory get failure");
                //exit(1);
            }
#   endif

        }

        MEMALIGN (mem_ref, align, size_t (mem_size));
        if (mem_ref)
        {
            memset (mem_ref, 0, mem_size);
            std::cout << "info string Hash " << (mem_size >> 20) << " MB..." << std::endl;
        }
    }

    void free_memory    (void *mem)
    {
        if (!mem) return;

        if (use_largepages)
        {

#   if defined(_WIN32) || defined(__CYGWIN__) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__BORLANDC__)
            VirtualFree (mem, 0, MEM_RELEASE);
#   else   // Linux - Unix
            shmdt  (mem);
            shmctl (num, IPC_RMID, NULL);
#   endif

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