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
