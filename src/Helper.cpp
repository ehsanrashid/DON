#include "Helper.h"

#include <cctype>
#include <algorithm>
#include <mutex>

using std::string;

bool whiteSpaces(string const &str) {
    return str.empty()
        || std::all_of(str.begin(), str.end(), ::isspace);
}

string& toLower(string &str) {
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
string& toUpper(string &str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}
string& toggle(string &str) {
    std::transform(str.begin(), str.end(), str.begin(),
        [](int ch) { return std::islower(ch) ? std::toupper(ch) : std::tolower(ch); });
    return str;
}
string& reverse(string &str) {
    std::reverse(str.begin(), str.end());
    return str;
}
string& replace(string &str, char const oldCh, char const newCh) {
    std::replace(str.begin(), str.end(), oldCh, newCh);
    return str;
}

string& ltrim(string& str) {
    str.erase(
        str.begin(),
        std::find_if(str.begin(), str.end(),
            [](int ch) { return !(std::isspace(ch) || ch == '\0'); }));
    return str;
}
string& rtrim(string& str) {
    str.erase(
        std::find_if(str.rbegin(), str.rend(),
            [](int ch) { return !(std::isspace(ch) || ch == '\0'); }).base(),
        str.end());
    return str;
}
string& trim(string &str) {
    /*
    auto beg{ str.find_first_not_of(' ') };
    if (beg != string::npos)
    {
        auto end{ str.find_last_not_of(' ') };
        str = str.substr(beg, (end - beg + 1));
    }
    */

    ltrim(str);
    rtrim(str);

    return str;
}

/// Wrappers for systems where the c++17 implementation doesn't guarantee the availability of aligned_alloc.
/// Memory allocated with std_aligned_alloc must be freed with std_aligned_free.

void* std_aligned_alloc(size_t alignment, size_t size) {
#if (defined(__APPLE__) && defined(_LIBCPP_HAS_C11_FEATURES)) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
    return aligned_alloc(alignment, size);
#elif (defined(_WIN32) || (defined(__APPLE__) && !defined(_LIBCPP_HAS_C11_FEATURES)))
    return _mm_malloc(size, alignment);
#else
    return std::aligned_alloc(alignment, size);
#endif
}

void std_aligned_free(void *ptr) {
#if (defined(__APPLE__) && defined(_LIBCPP_HAS_C11_FEATURES)) || defined(__ANDROID__) || defined(__OpenBSD__) || (defined(__GLIBCXX__) && !defined(_GLIBCXX_HAVE_ALIGNED_ALLOC) && !defined(_WIN32))
    free(ptr);
#elif (defined(_WIN32) || (defined(__APPLE__) && !defined(_LIBCPP_HAS_C11_FEATURES)))
    _mm_free(ptr);
#else
    free(ptr);
#endif
}

/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream &os, OutputState outputState) {
    static std::mutex Mutex;

    switch (outputState) {
    case OS_LOCK:
        Mutex.lock();
        break;
    case OS_UNLOCK:
        Mutex.unlock();
        break;
    }
    return os;
}
