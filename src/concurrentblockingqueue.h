


template<typename T>
class concurrent_blocking_queue : public noncopyable
{
    std::queue<T> m_internal_queue;
    mutable critical_section m_critical_section;
    condition_variable m_queue_full;
    condition_variable m_queue_empty;
    const size_t m_capacity;

public:
    concurrent_blocking_queue(size_t capacity) 
        : m_capacity(capacity), m_critical_section()
    {
    }

    void put(T const & input)
    {
        framework::scoped_lock lock(m_critical_section); //lock
        m_queue_full.wait(lock, [&]{ return !full(); });
        m_internal_queue.push(input);
        m_queue_empty.notify_all();
    }
    T take()
    {
        framework::scoped_lock lock(m_critical_section); //lock
        m_queue_empty.wait(lock, [&]{ return !empty(); });
        T output = m_internal_queue.front();
        m_internal_queue.pop();
        m_queue_full.notify_all();
        return output;
    }

    bool full() const
    {
        framework::scoped_lock lock(m_critical_section);
        if ( m_internal_queue.size() > m_capacity )
        {
            throw std::logic_error("size of concurrent_blocking_queue cannot be greater than the capacity.");
        }
        return m_internal_queue.size() == m_capacity;
    }

    bool empty() const
    {
        framework::scoped_lock lock(m_critical_section);
        return m_internal_queue.empty();
    }
    //..
};
