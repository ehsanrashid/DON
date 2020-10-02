#pragma once

#include <cctype>
#include <functional>
#include <string>
#include <string_view>

#include "type.h"

// Case-insensitive comparator for char

inline bool compareCaseInsensitiveLess(u08 ch1, u08 ch2) noexcept {
    return
        //toupper(ch1) < toupper(ch2);
        tolower(ch1) < tolower(ch2);
}

inline bool compareCaseInsensitiveMore(u08 ch1, u08 ch2) noexcept {
    return
        //toupper(ch1) > toupper(ch2);
        tolower(ch1) > tolower(ch2);
}

inline bool compareCaseInsensitiveEqual(u08 ch1, u08 ch2) noexcept {
    return
        //toupper(ch1) == toupper(ch2);
        tolower(ch1) == tolower(ch2);
}

// Case-insensitive comparator for string

struct CaseInsensitiveLessComparer {
    bool operator()(std::string_view s1, std::string_view s2) const {
        //return stricmp(s1.data(), s2.data()) < 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end()
                    , compareCaseInsensitiveLess);
    }
};

struct CaseInsensitiveMoreComparer {
    bool operator()(std::string_view s1, std::string_view s2) const {
        //return stricmp(s1.data(), s2.data()) > 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end()
                    , compareCaseInsensitiveMore);
    }
};

struct CaseInsensitiveEqualComparer {
    bool operator()(std::string_view s1, std::string_view s2) const {
        //return stricmp(s1.data(), s2.data()) == 0;
        return std::lexicographical_compare(s1.begin(), s1.end(), s2.begin(), s2.end()
                    , compareCaseInsensitiveEqual);
    }
};