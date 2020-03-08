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

Logger::Logger() :
    _tsbInnput{ std:: cin.rdbuf(), _ofs.rdbuf() },
    _tsbOutput{ std::cout.rdbuf(), _ofs.rdbuf() }
{}

Logger::~Logger() {

    setup("");
}

Logger& Logger::instance() {
    // Since it's a static variable, if the class has already been created,
    // it won't be created again.
    // And it is thread-safe in C++11.
    static Logger _instance;

    return _instance;
}

void Logger::setup(std::string const &fnLog) {
    if (_ofs.is_open()) {
        std::cout.rdbuf(_tsbOutput.sbRead);
        std:: cin.rdbuf(_tsbInnput.sbRead);

        _ofs << "[" << std::chrono::system_clock::now() << "] <-\n";
        _ofs.close();
    }

    _fnLog = fnLog;
    replace(_fnLog, '\\', '/');
    trim(_fnLog);
    if (whiteSpaces(_fnLog)) {
        _fnLog.clear();
        return;
    }

    _ofs.open(_fnLog, std::ios::out|std::ios::app);
    if (!_ofs.is_open()) {
        std::cerr << "Unable to open Log File " << _fnLog << std::endl;
        std::exit(EXIT_FAILURE);
    }
    _ofs << "[" << std::chrono::system_clock::now() << "] ->\n";

    std:: cin.rdbuf(&_tsbInnput);
    std::cout.rdbuf(&_tsbOutput);

}
