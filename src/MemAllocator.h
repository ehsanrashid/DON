#include <stdexcept>
#include <cstddef>
#include <new>

// Stack like allocator
class MemAllocator
{
    size_t used_size;
    size_t total_size;
    void *memory;

public:
    
    MemAllocator(size_t backing_size)
    {
        memory = new char[backing_size];
        used_size   = 0;
        total_size  = backing_size;
    }
    
    ~MemAllocator()
    {
        delete[] memory;
    }

    void* allocate (size_t desired_size)
    {
        // You would have to make sure alignment was correct for your
        // platform (Exercise to the reader)
        
        size_t new_used_size = used_size + desired_size;
        
        if (new_used_size > total_size)
        {
            throw ::std::bad_alloc (); //("Exceeded maximum size for this allocator.");
        }

        void *mem = static_cast<void*>(static_cast<char*>(memory) + used_size);
        used_size = new_used_size;

        return mem;
    }

    // If you need to support deallocation then modifying this shouldn't be too difficult
    void deallocate (void *mem)
    {

    }

};
