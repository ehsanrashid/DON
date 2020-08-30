#include "Helper.h"

#include <cctype>
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

#if defined(_WIN32)
    #include <direct.h>
    #define GETCWD(buff, size)  _getcwd(buff, size)
#else
    #include <unistd.h>
    #define GETCWD(buff, size)  getcwd(buff, size)
#endif

namespace CommandLine {

    string binaryDirectory;  // path of the executable directory
    string workingDirectory; // path of the working directory

    void initialize(int argc, char const *const *argv) {
        (void)argc;
        string separator;

        string argv0; // path+name of the executable binary, as given by argv[0]
        // Extract the path+name of the executable binary
        argv0 = argv[0];

        string pathSeparator; // Separator for our current OS
#if defined(_WIN32)
        pathSeparator = "\\";
    #if defined(_MSC_VER)
        // Under windows argv[0] may not have the extension. Also _get_pgmptr() had
        // issues in some windows 10 versions, so check returned values carefully.
        char* pgmptr = nullptr;
        if (!_get_pgmptr(&pgmptr) && pgmptr != nullptr && *pgmptr) {
            argv0 = pgmptr;
        }

    #endif
#else
        pathSeparator = "/";
#endif

        // Extract the working directory
        workingDirectory = "";
        char buff[40000];
        char* cwd = GETCWD(buff, sizeof (buff));
        if (cwd != nullptr) {
            workingDirectory = cwd;
        }
        // Extract the binary directory path from argv0
        binaryDirectory = argv0;
        size_t pos = binaryDirectory.find_last_of("\\/");
        if (pos == std::string::npos) {
            binaryDirectory = "." + pathSeparator;
        }
        else {
            binaryDirectory.resize(pos + 1);
        }

        // Pattern replacement: "./" at the start of path is replaced by the working directory
        if (binaryDirectory.find("." + pathSeparator) == 0) {
            binaryDirectory.replace(0, 1, workingDirectory);
        }
    }
}

