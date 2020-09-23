#pragma once

#include <streambuf>

// C++ way to prepare a buffer for a memory stream
template<class T>
class MemoryStreamBuffer :
    public std::basic_streambuf<T> {

public:
    using std::basic_streambuf<T>::basic_streambuf;

    MemoryStreamBuffer(MemoryStreamBuffer const &) = delete;
    MemoryStreamBuffer(MemoryStreamBuffer &&) = delete;

    MemoryStreamBuffer(T *p, size_t n) {
        std::basic_streambuf<T>::setg(p, p, p + n);
        std::basic_streambuf<T>::setp(p, p + n);
    }
};