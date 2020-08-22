#include "Logger.h"

#include <chrono>
#include <cstdlib>

#include "Helper.h"

#if defined(_WIN32)
    #include <ctime>
#endif

namespace {

    using SystemClockTimePoint = std::chrono::system_clock::time_point;

    std::string toString(SystemClockTimePoint const &timePoint) {
        std::string str;

    #if defined(_WIN32)

        time_t const cur_time{ std::chrono::system_clock::to_time_t(timePoint) };

        tm local_tm;
        localtime_s(&local_tm, (time_t const*)&cur_time);
        char const *format{ "%Y.%m.%d-%H.%M.%S" };
        char buffer[32];
        strftime(buffer, sizeof (buffer), format, (tm const*)&local_tm);
        str.append(buffer);

        auto ms{ std::chrono::duration_cast<std::chrono::milliseconds>
                    (timePoint - std::chrono::system_clock::from_time_t(cur_time)).count() };
        str.append(".");
        str.append(std::to_string(ms));
    #else

        (void)timePoint;

    #endif
        return str;
    }

    std::ostream& operator<<(std::ostream &os, SystemClockTimePoint const &timePoint) {
        os << toString(timePoint);
        return os;
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
    // Since it's a static variable, if the class has already been created,
    // it won't be created again.
    // And it is thread-safe in C++11.
    static Logger staticInstance;

    return staticInstance;
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
