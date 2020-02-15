#include "Logger.h"

#include <chrono>

#include "Engine.h"

#if defined(_WIN32)
#   include <ctime>
#endif

using namespace std;

namespace
{

    using SystemClockTimePoint = std::chrono::system_clock::time_point;

    string toString(const SystemClockTimePoint &tp)
    {
        string str = "";

#   if defined(_WIN32)

        auto time = chrono::system_clock::to_time_t(tp);

        const auto *ltm = localtime(&time);
        const char *format = "%Y.%m.%d-%H.%M.%S";
        char buffer[32];
        strftime(buffer, sizeof (buffer), format, ltm);
        str.append(buffer);

        auto ms = chrono::duration_cast<chrono::milliseconds>(tp - chrono::system_clock::from_time_t(time)).count();
        str.append(".");
        str.append(to_string(ms));

#   else

        (void)tp;

#   endif

        return str;
    }

    ostream& operator<<(ostream &os, const SystemClockTimePoint &tp)
    {
        os << toString(tp);
        return os;
    }

}

Logger::Logger()
    : ofs{}
    , iTSB{ cin.rdbuf(), ofs.rdbuf()}
    , oTSB{cout.rdbuf(), ofs.rdbuf()}
{}

Logger::~Logger()
{
    set("");
}

Logger& Logger::instance()
{
    // Since it's a static variable, if the class has already been created,
    // it won't be created again.
    // And it is thread-safe in C++11.
    static Logger _instance;

    return _instance;
}

void Logger::set(const string &logFn)
{
    if (ofs.is_open())
    {
        cout.rdbuf(oTSB.readSB);
         cin.rdbuf(iTSB.readSB);

        ofs << "[" << chrono::system_clock::now() << "] <-" << endl;
        ofs.close();
    }
    if (!logFn.empty())
    {
        ofs.open(logFn, ios::out|ios::app);
        if (!ofs.is_open())
        {
            cerr << "Unable to open Log File " << logFn << endl;
            stop(EXIT_FAILURE);
        }
        ofs << "[" << chrono::system_clock::now() << "] ->" << endl;

         cin.rdbuf(&iTSB);
        cout.rdbuf(&oTSB);
    }
}
