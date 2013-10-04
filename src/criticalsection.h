
#include <Windows.h>
#include "noncopyable.h"

class critical_section
    : public CRITICAL_SECTION
    , public std::noncopyable
{

 public:
    critical_section()
    {
        ::InitializeCriticalSection(this);
    }
    
    ~critical_section()
    {
        ::DeleteCriticalSection(this);
    }
    
    void lock()
    {
        ::EnterCriticalSection(this);
    }
    
    void unlock()
    {
        ::LeaveCriticalSection(this);
    }
};