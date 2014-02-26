#ifdef LPAGES

#include "MemoryHandler.h"

#include "UCI.h"

#ifndef _WIN32 // Linux - Unix

#   include <sys/ipc.h>
#   include <sys/shm.h>

#else

#   include <tchar.h>
#   include <stdio.h>

#   define MEMALIGN(mem, align, size)   mem = _aligned_malloc (size, align)
#   define ALIGNED_FREE(mem)           _aligned_free (mem);

#   define SE_PRIVILEGE_DISABLED       (0x00000000L)

#endif

namespace {

    bool use_large = false;

#   ifdef _WIN32

    void print_error (const TCHAR* psz_api, DWORD dw_error)
    {
        LPVOID lpv_message_buff;

        FormatMessage (FORMAT_MESSAGE_ALLOCATE_BUFFER |
                      FORMAT_MESSAGE_FROM_SYSTEM |
                      FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, dw_error,
                      MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),
                      (LPTSTR) (&lpv_message_buff), 0, NULL);

        // Now display the string
        _tprintf (TEXT ("ERROR: API        = %s\n"), psz_api);
        _tprintf (TEXT ("       Error code = %d\n"), dw_error);
        _tprintf (TEXT ("       Message    = %s\n"), lpv_message_buff);

        // Free the buffer allocated by the system
        LocalFree (lpv_message_buff);

        ExitProcess (GetLastError ());
    }
#   endif

}

namespace Memoryhandler {

    void setup_privileges (const TCHAR* psz_privilege, bool enable)
    {

#   ifdef _WIN32
        /// http://msdn.microsoft.com/en-us/library/aa366543%28VS.85%29.aspx

        HANDLE           token_handle;
        TOKEN_PRIVILEGES token_prlg;

        // Open process token
        if (!OpenProcessToken (
                  GetCurrentProcess ()
                , TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY
                , &token_handle))
        {
            print_error (TEXT (const_cast<TCHAR *>("OpenProcessToken")), GetLastError ());
        }

        // Get the LUID
        if (!LookupPrivilegeValue (NULL, psz_privilege, &token_prlg.Privileges[0].Luid))
        {
            print_error (TEXT (const_cast<TCHAR *>("LookupPrivilegeValue")), GetLastError ());
        }

        token_prlg.PrivilegeCount = 1;

        token_prlg.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        // enable or disable privilege
        token_prlg.Privileges[0].Attributes = (enable ? SE_PRIVILEGE_ENABLED : SE_PRIVILEGE_DISABLED);
        bool status = AdjustTokenPrivileges (token_handle, false, &token_prlg, 0, NULL, 0);

        // It is possible for AdjustTokenPrivileges to return TRUE and still not succeed.
        // So always check for the last error value.
        DWORD dw_error = GetLastError ();
        if (!status || (dw_error != ERROR_SUCCESS))
        {
            print_error (TEXT (const_cast<TCHAR *>("AdjustTokenPrivileges")), dw_error);
        }

        // close the handle
        if (!CloseHandle (token_handle))
        {
            print_error (TEXT (const_cast<TCHAR *>("CloseHandle")), GetLastError ());
        }

#   endif

    }

    void create_memory (void **mem_ref, uint64_t size, uint32_t align)
    {
        use_large = false;

        if (bool (*(Options["Large Pages"])))
        {

#   ifdef _WIN32

            /* Vlad0 */
            (*mem_ref) = VirtualAlloc (NULL, size, MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
            if ((*mem_ref)) /* HACK */
            {
                use_large = true;
                sync_cout << "info string WindowsLargePages " << (size >> 20) << "MB Hash..." << sync_endl;
            }
            else
            {
                MEMALIGN ((*mem_ref), align, size);
            }

#   else    // Linux - Unix

            int32_t num = shmget (IPC_PRIVATE, size, IPC_CREAT | SHM_R | SHM_W | SHM_HUGETLB);
            if (num != -1)
            {
                (*mem_ref) = shmat (num, NULL, 0x0);
                use_large = true;
                sync_cout << "info string HUGELTB " << (size >> 20) << "MB Hash..." << sync_endl;
            }
            else
            {
                MEMALIGN ((*mem_ref), align, size);
            }

#   endif

        }
        else
        {
            MEMALIGN ((*mem_ref), align, size);
        }

    }

    void free_memory (void *mem_ref)
    {
        if (!mem_ref) return;

        if (use_large)
        {

#ifdef _WIN32

            VirtualFree (mem_ref, 0, MEM_RELEASE);

#else   // Linux - Unix

            shmdt (mem_ref);
            shmctl (num, IPC_RMID, NULL);

#endif

        }
        else
        {
            ALIGNED_FREE (mem_ref);
        }
    }

}

#endif