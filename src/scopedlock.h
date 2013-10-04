

#include "criticalsection.h"
#include "noncopyable.h"

class scoped_lock : public std::noncopyable
{
    
private:
    friend class condition_variable;
    critical_section & m_cs;
public:
    
    scoped_lock (critical_section & cs)
        : m_cs(cs)
    {
        m_cs.lock();
    }
    
    ~scoped_lock ()
    {
        m_cs.unlock();
    }
};

