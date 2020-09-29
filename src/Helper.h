#pragma once

#include <cassert>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

#include "Type.h"

constexpr char toLower(char c) {
    return (c >= 'A' && c <= 'Z' ? (c - 'A') + 'a' : c);
}
constexpr char toUpper(char c) {
    return (c >= 'a' && c <= 'z' ? (c - 'a') + 'A' : c);
}

/// std::string Helpers

extern bool whiteSpaces(std::string_view);

extern std::string& toLower(std::string&);
extern std::string& toUpper(std::string&);

extern std::string toLower(std::string const&);
extern std::string toUpper(std::string const&);

extern std::string& toggle(std::string&);
extern std::string& reverse(std::string&);
extern std::string& replace(std::string&, char const, char const);

extern std::string& ltrim(std::string&);
extern std::string& rtrim(std::string&);
extern std::string& trim(std::string&);
extern std::vector<std::string> split(std::string_view, char);

template<typename Container, typename Key>
auto contains(Container const &c, Key const &k)-> bool {
    return c.find(k) != c.end();
}

namespace CommandLine {

    extern std::string binaryDirectory;  // path of the executable directory
    extern std::string workingDirectory; // path of the working directory

    extern void initialize(int, char const *const[]);
}


/*
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

    DelimitedIterator &operator=(const T &t) {
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
*/
