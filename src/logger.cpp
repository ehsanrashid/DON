#include "logger.h"

#include <cstdlib>
#include <chrono>

#include "helper.h"

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
        strftime(buffer, sizeof (buffer), format, (tm const*)&local_tm);
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

Logger::Logger() noexcept :
    itiestreambuf{ std:: cin.rdbuf(), ofstream.rdbuf() },
    otiestreambuf{ std::cout.rdbuf(), ofstream.rdbuf() } {
}

Logger::~Logger() {
    setup("");
}

Logger& Logger::instance() noexcept {
    // Since it's a static instance variable,
    // if the class has already been created, it won't be created again.
    // And it is thread-safe in C++11.
    static Logger logger;
    return logger;
}

void Logger::setup(std::string_view logFile) {
    if (ofstream.is_open()) {
        std::cout.rdbuf(otiestreambuf.rstreambuf);
        std:: cin.rdbuf(itiestreambuf.rstreambuf);

        ofstream << "[" << system_clock::now() << "] <-\n";
        ofstream.close();
    }

    filename = logFile;
    replace(filename, '\\', '/');
    trim(filename);
    if (whiteSpaces(filename)) {
        filename.clear();
        return;
    }

    ofstream.open(filename, std::ios::out|std::ios::app);
    if (!ofstream.is_open()) {
        std::cerr << "Unable to open Log File " << filename << '\n';
        std::exit(EXIT_FAILURE);
    }
    ofstream << "[" << system_clock::now() << "] ->\n";

    std:: cin.rdbuf(&itiestreambuf);
    std::cout.rdbuf(&otiestreambuf);
}
