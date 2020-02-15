#include "Helper.h"

#include <cctype>
#include <mutex>
#include <algorithm>

using namespace std;

/// Used to serialize access to std::cout to avoid multiple threads writing at the same time.
ostream& operator<<(ostream &os, OutputState outputState)
{
    static mutex mtx;

    switch (outputState)
    {
    case OS_LOCK:   mtx.lock();   break;
    case OS_UNLOCK: mtx.unlock(); break;
    default: break;
    }
    return os;
}

bool whiteSpaces(const string &str)
{
    return str.empty()
        || std::all_of(str.begin(), str.end(), ::isspace);
}

string& toLower(string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::tolower);
    return str;
}
string& toUpper(string &str)
{
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}
string& toggleCase(string &str)
{
    std::transform(str.begin(), str.end(), str.begin(),
                   [](int c) -> int
                   { return ::islower(c) ? ::toupper(c) : ::tolower(c); });
    return str;
}

string trim(string &str)
{
    auto beg = str.find_first_not_of(' ');
    if (beg != string::npos)
    {
        auto end = str.find_last_not_of(' ');
        str = str.substr(beg, (end - beg + 1));
    }
    return str;
}

string appendPath(const string &basePath, const string &filePath)
{
    return basePath[basePath.length() - 1] != '/' ?
            basePath + '/' + filePath :
            basePath + filePath;
}
void removeExtension(string &filename)
{
   auto pos = filename.find_last_of('.');
   if (pos != string::npos)
   {
       //filename = filename.substr(0, pos);
       filename.erase(pos);
   }
}


// void eraseSubstring(string &str, const string &sub)
// {
//     auto pos = str.find(sub);
//     while (pos != string::npos)
//     {
//         str.erase(pos, sub.length());
//         pos = str.find(sub);
//     }
// }
// void eraseSubstring(string &str, const vector<string> &subList)
// {
//     std::for_each(subList.begin(), subList.end(), std::bind(eraseSubstring, std::ref(str), std::placeholders::_1));
// }

// vector<string> splitString(const string &str, char delimiter = ' ', bool keepEmpty = true, bool doTrim = false)
// {
//    vector<string> tokens;
//    istringstream iss{str};
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
// }
