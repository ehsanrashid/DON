#if defined(LPAGES)

#include "MemoryHandler.h"

#include <iostream>

#include "Option.h"
#include "Thread.h"

#if defined(_WIN32)

#   include <cstdio>
#   include <tchar.h>

#   if !defined(NOMINMAX)
#       define NOMINMAX // Disable macros min() and max()
#   endif
#   if !defined(WIN32_LEAN_AND_MEAN)
#       define WIN32_LEAN_AND_MEAN
#   endif
#   include <windows.h>
#   undef WIN32_LEAN_AND_MEAN
#   undef NOMINMAX

#   define SE_PRIVILEGE_DISABLED       (0x00000000L)

#   define ALLOC_ALIGNED(mem, size, alignment) mem=_aligned_malloc(size, alignment)
#   define FREE_ALIGNED(mem)                       _aligned_free(mem)

#else

#   include <sys/ipc.h>
#   include <sys/mman.h>
#   include <sys/shm.h>

#   ifndef SHM_HUGETLB
#       define SHM_HUGETLB     04000
#   endif

#   define ALLOC_ALIGNED(mem, size, alignment) posix_memalign(&mem, alignment, size)
#   define FREE_ALIGNED(mem)                   free(mem)

#endif

namespace Memory {

    using namespace std;

    namespace {

        bool PagesUsed = false;

#   if defined(_WIN32)

        bool setup_privilege(const char *privilege_name, bool enable)
        {
            bool ret = false;
            HANDLE token_handle;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY, &token_handle))
            {
                TOKEN_PRIVILEGES token_priv;
                if (LookupPrivilegeValue(nullptr, privilege_name, &token_priv.Privileges[0].Luid))
                {
                    token_priv.PrivilegeCount = 1;
                    token_priv.Privileges[0].Attributes = enable ?
                                                            SE_PRIVILEGE_ENABLED :
                                                            SE_PRIVILEGE_DISABLED;
                    if (AdjustTokenPrivileges(token_handle, false, &token_priv, 0, nullptr, nullptr))
                    {
                        if (GetLastError() != ERROR_NOT_ALL_ASSIGNED)
                        {
                            ret = true;
                        }
                    }
                    CloseHandle(token_handle);
                }
            }
            return ret;
        }

#   else

        i32 SHM; // Shared Memory Identifier

#   endif

    }

    void alloc_memory(void *&mem_ref, size_t mem_size, u32 alignment)
    {
        PagesUsed = false;

        if (bool(Options["Large Pages"]))
        {
#       if defined(_WIN32)

            mem_ref = VirtualAlloc(nullptr,                               // System selects address
                                   mem_size,                              // Size of allocation
                                   MEM_LARGE_PAGES|MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                                   PAGE_READWRITE);                       // Protection of Allocation
            if (nullptr != mem_ref)
            {
                PagesUsed = true;
                sync_cout << "info string Large Pages Hash " << (mem_size >> 20) << " MB" << sync_endl;
                return;
            }
            mem_ref = VirtualAlloc(nullptr,              // System selects address
                                   mem_size,              // Size of allocation
                                   MEM_COMMIT|MEM_RESERVE,// Type of Allocation
                                   PAGE_READWRITE);       // Protection of Allocation
            if (nullptr != mem_ref)
            {
                PagesUsed = true;
                sync_cout << "info string Normal Pages Hash " << (mem_size >> 20) << " MB" << sync_endl;
                return;
            }
            cerr << "ERROR: VirtualAlloc() virtual memory alloc failed " << (mem_size >> 20) << " MB" << endl;

#       else

            SHM = shmget(IPC_PRIVATE,
                         mem_size,
                         IPC_CREAT|SHM_R|SHM_W|SHM_HUGETLB);
            if (SHM != -1)
            {
                mem_ref = shmat(SHM, nullptr, 0x00);
                if (mem_ref != (void*) -1)
                {
                    PagesUsed = true;
                    sync_cout << "info string Large Pages Hash " << (mem_size >> 20) << " MB" << sync_endl;
                    return;
                }
                cerr << "ERROR: shmat() shared memory attach failed " << (mem_size >> 20) << " MB" << endl;
                if (shmctl (SHM, IPC_RMID, nullptr) == -1)
                {
                    cerr << "ERROR: shmctl(IPC_RMID) failed" << endl;
                }
                return;
            }
            SHM = shmget(IPC_PRIVATE,
                         mem_size,
                         IPC_CREAT|SHM_R|SHM_W);
            if (SHM != -1)
            {
                mem_ref = shmat(SHM, nullptr, 0x00);
                if (mem_ref != (void*) -1)
                {
                    PagesUsed = true;
                    sync_cout << "info string Normal Pages Hash " << (mem_size >> 20) << " MB" << sync_endl;
                    return;
                }
                cerr << "ERROR: shmat() shared memory attach failed " << (mem_size >> 20) << " MB" << endl;
                if (shmctl(SHM, IPC_RMID, nullptr) == -1)
                {
                    cerr << "ERROR: shmctl(IPC_RMID) failed" << endl;
                }
                return;
            }
            cerr << "ERROR: shmget() shared memory alloc failed " << (mem_size >> 20) << " MB" << endl;

#       endif
        }

        ALLOC_ALIGNED(mem_ref, mem_size, alignment);
        if (nullptr != mem_ref)
        {
            sync_cout << "info string No Pages Hash " << (mem_size >> 20) << " MB" << sync_endl;
            return;
        }

        cerr << "ERROR: Hash memory allocate failed " << (mem_size >> 20) << " MB" << endl;
    }

    void free_memory(void *mem)
    {
        if (nullptr == mem)
        {
            return;
        }

        if (PagesUsed)
        {
#       if defined(_WIN32)

            if (!VirtualFree(mem, 0, MEM_RELEASE))
            {
                cerr << "ERROR: VirtualFree() virtual memory free failed" << endl;
            }

#       else

            if (shmdt(mem) == -1)
            {
                cerr << "ERROR: shmdt() shared memory detach failed" << endl;
            }
            if (shmctl(SHM, IPC_RMID, nullptr) == -1)
            {
                cerr << "ERROR: shmctl(IPC_RMID) failed"<< endl;
            }

#       endif
        }
        else
        {
            FREE_ALIGNED(mem);
        }
    }

    void initialize()
    {
#   if defined(_WIN32)

        setup_privilege(SE_LOCK_MEMORY_NAME, true);

#   else

#   endif
    }

    void deinitialize()
    {
#   if defined(_WIN32)

        setup_privilege(SE_LOCK_MEMORY_NAME, false);

#   else

#   endif
    }

}

#endif // LPAGES
