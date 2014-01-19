#include "Engine.h"

#include <sstream>
#include <iomanip>
#include <iostream>

#include "BitBoard.h"
#include "BitBases.h"
#include "Pawns.h"
#include "Material.h"
#include "Evaluator.h"
#include "Searcher.h"
#include "Transposition.h"
#include "UCI.h"
#include "Tester.h"
#include "IOLogger.h"

//#include <thread>
#include "Thread.h"

namespace Engine {

    using namespace std;

    namespace {

        const string Name      = "DON";

        // Version number.
        // If Version is left empty, then compile date in the format DD-MM-YY.
        const string Version   = "1.0b";
        const string Author    = "Ehsan Rashid";

        const string Months ("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");

    }

    string info (bool uci)
    {
        ostringstream ss;

        if (uci) ss << "id name ";
        ss << Name << " ";

        if (Version.empty ())
        {
            // From compiler, format is "Sep 2 2013"
            stringstream date (__DATE__);
            string
                month,
                day,
                year;

            date 
                >> month
                >> day
                >> year;

            ss << setfill ('0')
                << setw (2) << day //<< '-'
                << setw (2) << (Months.find (month) / 4 + 1) //<< '-'
                << setw (2) << year.substr (2);
        }
        else
        {
            ss << Version;
        }

#ifdef _64BIT
        ss << " x64";
#else
        ss << " w32";
#endif

        //#ifdef POPCNT
        //        ss << " SSE4.2";
        //#endif

        ss  << "\n" 
            << ((uci) ? "id author " : "(c) 2014 ")
            << Author << "\n";

        return ss.str ();
    }

    void run (const std::string &args)
    {
        cout << Engine::info () << endl;

        cout 
            << "info string " << cpu_count () << " processor(s) found."
#ifdef POPCNT
            << " POPCNT available."
#endif
            << endl;

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Zobrist  ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Searcher ::initialize ();
        Pawns    ::initialize ();
        Evaluator::initialize ();
        Threads   .initialize ();

        cout
            << "info string " << Threads.size () << " thread(s)." << "\n"
            << "info string " << TT.size ()      << " MB Hash."   << endl;

#ifdef _DEBUG
        //Tester::main_test ();
        //system ("pause");
        //return;
#endif
        //log_io (true);

        UCI   ::start (args);

        //log_io (false);

    }

    // Exit in case of error with error code
    void exit (int32_t code)
    {
        UCI::stop ();
        Threads.deinitialize ();
        UCI   ::deinitialize ();

        ::exit (code);
    }

}