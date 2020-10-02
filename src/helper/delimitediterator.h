#pragma once

#include <cassert>
#include <string>

template<class T, class Elem = char, class Traits = std::char_traits<Elem>>
struct DelimitedIterator {

public:
    using iterator_category = std::output_iterator_tag;
    using value_type = void;
    using difference_type = void;
    using pointer = void;
    using reference = void;

    using char_type = Elem;
    using traits_type = Traits;
    using ostream_type = std::basic_ostream<Elem, Traits>;

    DelimitedIterator(ostream_type &os, const Elem *const delimiter) :
        os(&os),
        delimiter(delimiter),
        first(true) {
    }

    DelimitedIterator &operator++() {
        return *this;
    }
    DelimitedIterator &operator++(int) {
        return *this;
    }
    DelimitedIterator &operator*() {
        return *this;
    }

    DelimitedIterator &operator=(T const &t) {
        if (first) {
            first = false;
        }
        else {
            if (delimiter) {
                *os << delimiter;
            }
        }

        *os << t;
        return *this;
    }

protected:
    ostream_type *os;
    const Elem *delimiter;
    bool first;
};

