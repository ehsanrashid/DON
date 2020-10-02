#pragma once

#include <streambuf>

// C++ way to prepare a buffer for a memory stream
template<class T>
class MemoryStreamBuffer :
    public std::basic_streambuf<T> {

public:
    using std::basic_streambuf<T>::basic_streambuf;

    MemoryStreamBuffer(MemoryStreamBuffer const&) = delete;
    MemoryStreamBuffer(MemoryStreamBuffer&&) = delete;

    MemoryStreamBuffer(T *ptr, size_t size) {
        std::basic_streambuf<T>::setg(ptr, ptr, ptr + size);
        std::basic_streambuf<T>::setp(ptr, ptr + size);
    }
};
