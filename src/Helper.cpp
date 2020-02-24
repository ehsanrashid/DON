#include "Helper.h"

#include <cctype>
#include <mutex>
#include <algorithm>

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
                   [](int c) -> int {
                        return ::islower(c) ? ::toupper(c) : ::tolower(c);
                   });
    return str;
}
string& reverse(string &str) {
    std::reverse(str.begin(), str.end());
    return str;
}
string& replace(string &str, char oldCh, char newCh) {
    std::replace(str.begin(), str.end(), oldCh, newCh);
    return str;
}
string& trim(string &str) {
    auto beg{ str.find_first_not_of(' ') };
    if (beg != string::npos)
    {
        auto end{ str.find_last_not_of(' ') };
        str = str.substr(beg, (end - beg + 1));
    }
    return str;
}

string appendPath(string const &basePath, string const &filePath) {
    return basePath[basePath.length() - 1] != '/' ?
            basePath + '/' + filePath :
            basePath + filePath;
}
void removeExtension(string &filename) {
    auto pos{ filename.find_last_of('.') };
    if (pos != string::npos)
    {
        //filename = filename.substr(0, pos);
        filename.erase(pos);
    }
}

//void eraseSubstring(string &str, string const &sub) {
//    auto pos{ str.find(sub) };
//    while (pos != string::npos)
//    {
//        str.erase(pos, sub.length());
//        pos = str.find(sub);
//    }
//}
//void eraseSubstring(string &str, const std::vector<string> &subList) {
//    std::for_each(subList.begin(), subList.end(), std::bind(eraseSubstring, std::ref(str), std::placeholders::_1));
//}

//std::vector<string> splitString(string const &str, char delimiter = ' ', bool keepEmpty = true, bool doTrim = false) {
//    std::vector<string> tokens;
//    istringstream iss{ str };
//    while (iss.good())
//    {
//        string token;
//        bool fail = !std::getline(iss, token, delimiter);
//        if (doTrim)
//        {
//            token = trim(token);
//        }
//        if (keepEmpty
//         || !token.empty())
//        {
//            tokens.push_back(token);
//        }
//        if (fail)
//        {
//            break;
//        }
//    }
//    return tokens;
//}


/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
std::ostream& operator<<(std::ostream &os, OutputState outputState) {
    static std::mutex mutex;
    if (outputState == OS_LOCK)     mutex.lock();
    else
    if (outputState == OS_UNLOCK)   mutex.unlock();
    return os;
}
