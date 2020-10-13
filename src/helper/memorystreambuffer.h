#pragma once

#include <streambuf>
#include <string>

/// C++ way to prepare a buffer for a memory stream
template<class Elem>
class MemoryStreamBuffer :
    public std::basic_streambuf<Elem, std::char_traits<Elem>> {

public:

    //using std::basic_streambuf<Elem, std::char_traits<Elem>>::basic_streambuf;

    MemoryStreamBuffer(Elem *ptr, size_t size) {
        this->setg(ptr, ptr, ptr + size);
        this->setp(ptr, ptr + size);
    }

};
