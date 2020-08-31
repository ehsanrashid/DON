#include "Logger.h"

#include <cstdlib>
#include <chrono>

#include "Helper.h"

#if defined(_WIN32)
    #include <ctime>
#endif

namespace {

    std::string toString(std::chrono::system_clock::time_point const &timePoint) {
        std::string str;

    #if defined(_WIN32)
        time_t const raw_time{ std::chrono::system_clock::to_time_t(timePoint) };
        tm local_tm;
        local_tm = *localtime(&raw_time);
        //localtime_s(&local_tm, (time_t const*)&raw_time);
        char const *format{ "%Y.%m.%d-%H.%M.%S" };
        char buffer[32];
        strftime(buffer, sizeof (buffer), format, (tm const*)&local_tm);
        str.append(buffer);
        // Append milli-second
        auto ms{ std::chrono::duration_cast<std::chrono::milliseconds>
                    (timePoint - std::chrono::system_clock::from_time_t(raw_time)).count() };
        str.append(".");
        str.append(std::to_string(ms));
    #else
        (void)timePoint;
    #endif
        return str;
    }

    std::ostream& operator<<(std::ostream &ostream, std::chrono::system_clock::time_point const &timePoint) {
        ostream << toString(timePoint);
        return ostream;
    }

}

Logger::Logger() :
    iStreamBuf{ std:: cin.rdbuf(), logFileStream.rdbuf() },
    oStreamBuf{ std::cout.rdbuf(), logFileStream.rdbuf() }
{}

Logger::~Logger() {
    setup("");
}

Logger& Logger::instance() {
    // Since it's a static instance variable,
    // if the class has already been created, it won't be created again.
    // And it is thread-safe in C++11.
    static Logger logger;
    return logger;
}

void Logger::setup(std::string const &file) {
    if (logFileStream.is_open()) {
        std::cout.rdbuf(oStreamBuf.sbRead);
        std:: cin.rdbuf(iStreamBuf.sbRead);

        logFileStream << "[" << std::chrono::system_clock::now() << "] <-\n";
        logFileStream.close();
    }

    logFile = file;
    replace(logFile, '\\', '/');
    trim(logFile);
    if (whiteSpaces(logFile)) {
        logFile.clear();
        return;
    }

    logFileStream.open(logFile, std::ios::out|std::ios::app);
    if (!logFileStream.is_open()) {
        std::cerr << "Unable to open Log File " << logFile << '\n';
        std::exit(EXIT_FAILURE);
    }
    logFileStream << "[" << std::chrono::system_clock::now() << "] ->\n";

    std:: cin.rdbuf(&iStreamBuf);
    std::cout.rdbuf(&oStreamBuf);
}
