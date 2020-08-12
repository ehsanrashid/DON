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

        time_t const time{ std::chrono::system_clock::to_time_t(tp) };

        tm ltm;
        localtime_s(&ltm, (time_t const*)&time);
        char const *format{ "%Y.%m.%d-%H.%M.%S" };
        char buffer[32];
        strftime(buffer, sizeof (buffer), format, (tm const*)&ltm);
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

Logger::Logger() :
    tsbInnput{ std:: cin.rdbuf(), ofs.rdbuf() },
    tsbOutput{ std::cout.rdbuf(), ofs.rdbuf() }
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

void Logger::setup(std::string const &fn) {
    if (ofs.is_open()) {
        std::cout.rdbuf(tsbOutput.sbRead);
        std:: cin.rdbuf(tsbInnput.sbRead);

        ofs << "[" << std::chrono::system_clock::now() << "] <-\n";
        ofs.close();
    }

    ofn = fn;
    replace(ofn, '\\', '/');
    trim(ofn);
    if (whiteSpaces(ofn)) {
        ofn.clear();
        return;
    }

    ofs.open(ofn, std::ios::out|std::ios::app);
    if (!ofs.is_open()) {
        std::cerr << "Unable to open Log File " << ofn << std::endl;
        std::exit(EXIT_FAILURE);
    }
    ofs << "[" << std::chrono::system_clock::now() << "] ->\n";

    std:: cin.rdbuf(&tsbInnput);
    std::cout.rdbuf(&tsbOutput);
}
