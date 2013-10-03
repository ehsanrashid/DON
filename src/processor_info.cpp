#include <windows.h>
#include <malloc.h>    
#include <stdio.h>
#include <tchar.h>

typedef BOOL (WINAPI *LPFN_GLPI)(PSYSTEM_LOGICAL_PROCESSOR_INFORMATION, PDWORD);

// Helper function to count set bits in the processor mask.
DWORD count_set_bits(ULONG_PTR bit_mask)
{
    DWORD LSHIFT = sizeof (ULONG_PTR)*8 - 1;
    DWORD bit_set_count = 0;
    ULONG_PTR bit_test = (ULONG_PTR)1 << LSHIFT;    

    for (DWORD i = 0; i <= LSHIFT; ++i)
    {
        bit_set_count += ((bit_mask & bit_test) ? 1 : 0);
        bit_test /= 2;
    }

    return bit_set_count;
}

int _cdecl main ()
{
    LPFN_GLPI glpi;
    BOOL done = FALSE;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION buffer = NULL;
    PSYSTEM_LOGICAL_PROCESSOR_INFORMATION ptr = NULL;
    DWORD return_length = 0;
    DWORD logical_processor_count = 0;
    DWORD numa_node_count = 0;
    DWORD processor_core_count = 0;
    DWORD processor_L1_cache_count = 0;
    DWORD processor_L2_cache_count = 0;
    DWORD processor_L3_cache_count = 0;
    DWORD processor_package_count = 0;
    DWORD byte_offset = 0;
    PCACHE_DESCRIPTOR cache_info;

    glpi = (LPFN_GLPI) GetProcAddress(
        GetModuleHandle(TEXT("kernel32")),
        "GetLogicalProcessorInformation");

    if (NULL == glpi) 
    {
        _tprintf(TEXT("\nGetLogicalProcessorInformation is not supported.\n"));
        return (1);
    }

    while (!done)
    {
        DWORD rc = glpi(buffer, &return_length);
        _tprintf(TEXT("\nLength = %d\n"), return_length);

        if (FALSE == rc) 
        {
            if (GetLastError() == ERROR_INSUFFICIENT_BUFFER) 
            {
                if (buffer)
                {
                    free(buffer);
                }

                buffer = (PSYSTEM_LOGICAL_PROCESSOR_INFORMATION) malloc (return_length);

                if (NULL == buffer) 
                {
                    _tprintf(TEXT("\nError: Allocation failure\n"));
                    return (2);
                }
            } 
            else 
            {
                _tprintf(TEXT("\nError %d\n"), GetLastError());
                return (3);
            }
        } 
        else
        {
            done = TRUE;
        }
    }

    ptr = buffer;

    while (byte_offset + sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION) <= return_length) 
    {
        switch (ptr->Relationship) 
        {
        case RelationNumaNode:
            // Non-NUMA systems report a single record of this type.
            numa_node_count++;
            break;

        case RelationProcessorCore:
            processor_core_count++;

            // A hyperthreaded core supplies more than one logical processor.
            logical_processor_count += count_set_bits(ptr->ProcessorMask);
            break;

        case RelationCache:
            // cache info data is in ptr->Cache, one CACHE_DESCRIPTOR structure for each cache. 
            cache_info = &ptr->Cache;
            if (cache_info->Level == 1)
            {
                processor_L1_cache_count++;
            }
            else if (cache_info->Level == 2)
            {
                processor_L2_cache_count++;
            }
            else if (cache_info->Level == 3)
            {
                processor_L3_cache_count++;
            }

            break;

        case RelationProcessorPackage:
            // Logical processors share a physical package.
            processor_package_count++;
            break;

        default:
            _tprintf(TEXT("\nError: Unsupported LOGICAL_PROCESSOR_RELATIONSHIP value.\n"));
            break;
        }
        byte_offset += sizeof (SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        ptr++;
    }

    _tprintf(TEXT("\nGetLogicalProcessorInformation results:\n"));
    _tprintf(TEXT("Number of NUMA nodes: %d\n"),
        numa_node_count);
    _tprintf(TEXT("Number of physical processor packages: %d\n"),
        processor_package_count);
    _tprintf(TEXT("Number of processor cores: %d\n"),
        processor_core_count);
    _tprintf(TEXT("Number of logical processors: %d\n"),
        logical_processor_count);
    _tprintf(TEXT("Number of processor L1/L2/L3 caches: %d/%d/%d\n"), 
        processor_L1_cache_count,
        processor_L2_cache_count,
        processor_L3_cache_count);
    _tprintf(TEXT("size of cache line: %d\n"),
        cache_info->LineSize);

    free (buffer);

    system ("pause");
    return 0;
}
