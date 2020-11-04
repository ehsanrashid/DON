#include "logger.h"

#include <cstdlib>
#include <chrono>

#include "string_view.h"

#if defined(_WIN32)
    #include <ctime>
#endif

namespace {

    using namespace std::chrono;

    std::string toString(system_clock::time_point const &timePoint) {
        std::string str;

    #if defined(_WIN32)
        time_t const raw_time{ system_clock::to_time_t(timePoint) };
        tm local_tm;
        local_tm = *localtime(&raw_time);
        //localtime_s(&local_tm, (time_t const*)&raw_time);
        char const *format{ "%Y.%m.%d-%H.%M.%S" };
        char buffer[32];
        strftime(buffer, sizeof(buffer), format, (tm const*)&local_tm);
        str.append(buffer);
        // Append milli-second
        auto ms{ duration_cast<milliseconds>
                    (timePoint - system_clock::from_time_t(raw_time)).count() };
        str.append(".");
        str.append(std::to_string(ms));
    #else
        (void)timePoint;
    #endif
        return str;
    }

    std::ostream& operator<<(std::ostream &ostream, system_clock::time_point const &timePoint) {
        ostream << toString(timePoint);
        return ostream;
    }

}

Logger::Logger(std::istream &is, std::ostream &os) noexcept :
    istream{ is },
    ostream{ os },
    itiestreambuf{ istream.rdbuf(), ofstream.rdbuf() },
    otiestreambuf{ ostream.rdbuf(), ofstream.rdbuf() } {
}

Logger::~Logger() {
    setup("");
}

void Logger::setup(std::string_view logFile) {

    if (ofstream.is_open()) {
        istream.rdbuf(itiestreambuf.rstreambuf);
        ostream.rdbuf(otiestreambuf.rstreambuf);

        ofstream << "[" << system_clock::now() << "] <-\n";
        ofstream.close();
    }

    filename = logFile;
    std::replace(filename.begin(), filename.end(), '\\', '/');
    filename = trim(filename);
    if (filename.empty()) {
        return;
    }

    ofstream.open(filename, std::ios::out|std::ios::app);
    if (!ofstream.is_open()) {
        std::cerr << "Unable to open Log File " << filename << '\n';
        std::exit(EXIT_FAILURE);
    }
    ofstream << "[" << system_clock::now() << "] ->\n";

    istream.rdbuf(&itiestreambuf);
    ostream.rdbuf(&otiestreambuf);
}
