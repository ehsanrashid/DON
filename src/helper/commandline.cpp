#include "commandline.h"

#if defined(_WIN32)
    #include <direct.h>
    #define GETCWD(buff)  _getcwd(buff, sizeof(buff))
#else
    #include <unistd.h>
    #define GETCWD(buff)   getcwd(buff, sizeof(buff))
#endif

#include <algorithm>

namespace CommandLine {

    std::string binaryDirectory;  // path of the executable directory
    std::string workingDirectory; // path of the working directory

    // path+name of the executable binary, as given by argv[0]
    void initialize(std::string argv0) {

        std::string pathSeparator; // Separator for our current OS
#if defined(_WIN32)
        pathSeparator = "\\";
    #if defined(_MSC_VER)
        // Under windows argv[0] may not have the extension. Also _get_pgmptr() had
        // issues in some windows 10 versions, so check returned values carefully.
        char *pgmptr = nullptr;
        if (!_get_pgmptr(&pgmptr)
         && pgmptr != nullptr
         && *pgmptr != 0) {
            argv0 = pgmptr;
        }
    #endif
#else
        pathSeparator = "/";
#endif

        // Extract the working directory
        workingDirectory = "";
        char buff[40000];
        
        char const *cwd = GETCWD(buff);
        if (cwd != nullptr) {
            workingDirectory = cwd;
        }
        // Extract the binary directory path from argv0
        binaryDirectory = argv0;
        size_t const pos = binaryDirectory.find_last_of("\\/");
        if (pos == std::string::npos) {
            binaryDirectory = "." + pathSeparator;
        } else {
            binaryDirectory.resize(pos + 1);
        }

        // Pattern replacement: "./" at the start of path is replaced by the working directory
        if (binaryDirectory.find("." + pathSeparator) == 0) {
            binaryDirectory.replace(0, 1, workingDirectory);
        }
        std::replace(binaryDirectory.begin(), binaryDirectory.end(), '\\', '/');
    }
}

