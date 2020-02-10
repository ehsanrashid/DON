#include "Logger.h"

#include <atomic>
#include <iomanip>

#if defined(_WIN32)
#   include <ctime>
#endif

#include "Engine.h"
#include "Util.h"

Logger Log;

using namespace std;

string toString(const chrono::system_clock::time_point &tp)
{
    string stime;

#   if defined(_WIN32)

    auto time = chrono::system_clock::to_time_t(tp);
    const auto *ltm = localtime(&time);
    const char *format = "%Y.%m.%d-%H.%M.%S";
    char buffer[32];
    strftime(buffer, sizeof (buffer), format, ltm);
    stime.append(buffer);
    auto ms = chrono::duration_cast<chrono::milliseconds>(tp - chrono::system_clock::from_time_t(time)).count();
    stime.append(".");
    stime.append(to_string(ms));

#   else

    (void)tp;

#   endif

    return stime;
}

Logger::Logger()
    : _iTieStreamBuf( cin.rdbuf(), _ofStream.rdbuf())
    , _oTieStreamBuf(cout.rdbuf(), _ofStream.rdbuf())
{}
Logger::~Logger()
{
    set("<empty>");
}

void Logger::set(const string &fn)
{
    if (_ofStream.is_open())
    {
        cout.rdbuf(_oTieStreamBuf.rStreamBuf);
         cin.rdbuf(_iTieStreamBuf.rStreamBuf);

        _ofStream << "[" << chrono::system_clock::now() << "] <-" << endl;
        _ofStream.close();
    }
    if (!whiteSpaces(fn))
    {
        _ofStream.open(fn, ios_base::out|ios_base::app);
        if (!_ofStream.is_open())
        {
            cerr << "Unable to open debug log file " << fn << endl;
            stop(EXIT_FAILURE);
        }
        _ofStream << "[" << chrono::system_clock::now() << "] ->" << endl;

         cin.rdbuf(&_iTieStreamBuf);
        cout.rdbuf(&_oTieStreamBuf);
    }
}

namespace {

    atomic<u64> CondCount;
    atomic<u64> HitCount;

    atomic<u64> ItemCount;
    atomic<i64> ItemSum;
}

void initializeDebug()
{
    CondCount = 0;
    HitCount = 0;

    ItemCount = 0;
    ItemSum = 0;
}

void debugHit(bool hit)
{
    ++CondCount;
    if (hit)
    {
        ++HitCount;
    }
}

void debugHitOn(bool cond, bool hit)
{
    if (cond)
    {
        debugHit(hit);
    }
}

void debugMeanOf(i64 item)
{
    ++ItemCount;
    ItemSum += item;
}

void debugPrint()
{
    if (0 != CondCount)
    {
        cerr << right
             << "---------------------------\n"
             << "Cond  :" << setw(20) << CondCount << "\n"
             << "Hit   :" << setw(20) << HitCount  << "\n"
             << "Rate  :" << setw(20) << fixed << setprecision(2) << double(HitCount) / CondCount * 100.0
             << left << endl;
    }

    if (0 != ItemCount)
    {
        cerr << right
             << "---------------------------\n"
             << "Count :" << setw(20) << ItemCount << "\n"
             << "Sum   :" << setw(20) << ItemSum << "\n"
             << "Mean  :" << setw(20) << fixed << setprecision(2) << double(ItemSum) / ItemCount
             << left << endl;
    }
}
