#include "Logger.h"

#include <chrono>
#include <cstdlib>

#include "Helper.h"

#if defined(_WIN32)
#   include <ctime>
#endif

namespace {

    using SystemClockTimePoint = std::chrono::system_clock::time_point;

    std::string toString(SystemClockTimePoint const &tp) {
        std::string str;

#   if defined(_WIN32)

        auto time = std::chrono::system_clock::to_time_t(tp);

        auto const *ltm = localtime(&time);
        char const *format = "%Y.%m.%d-%H.%M.%S";
        char buffer[32];
        strftime(buffer, sizeof (buffer), format, ltm);
        str.append(buffer);

        auto ms{ std::chrono::duration_cast<std::chrono::milliseconds>
                    (tp - std::chrono::system_clock::from_time_t(time)).count() };
        str.append(".");
        str.append(std::to_string(ms));

#   else

        (void)tp;

#   endif

        return str;
    }

    std::ostream& operator<<(std::ostream &os, SystemClockTimePoint const &tp) {
        os << toString(tp);
        return os;
    }

}

Logger::Logger()
    : ofs{}
    , iTSB{ std:: cin.rdbuf(), ofs.rdbuf() }
    , oTSB{ std::cout.rdbuf(), ofs.rdbuf() }
{}

Logger::~Logger() {
    setFile("");
}

Logger& Logger::instance() {
    // Since it's a static variable, if the class has already been created,
    // it won't be created again.
    // And it is thread-safe in C++11.
    static Logger _instance;

    return _instance;
}

void Logger::setFile(std::string const &lFn) {
    if (ofs.is_open()) {
        std::cout.rdbuf(oTSB.readSB);
        std::cin.rdbuf(iTSB.readSB);

        ofs << "[" << std::chrono::system_clock::now() << "] <-\n";
        ofs.close();
    }

    logFn = lFn;
    replace(logFn, '\\', '/');
    trim(logFn);
    if (logFn.empty()) {
        return;
    }

    ofs.open(logFn, std::ios::out | std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "Unable to open Log File " << logFn << std::endl;
        exit(EXIT_FAILURE);
    }
    ofs << "[" << std::chrono::system_clock::now() << "] ->\n";

    std::cin.rdbuf(&iTSB);
    std::cout.rdbuf(&oTSB);

}
