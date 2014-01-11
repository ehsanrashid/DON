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
#include "Thread.h"
#include "UCI.h"
#include "Tester.h"
#include "IOLogger.h"

namespace Engine {

    using namespace std;

    namespace {

        const string Engine    = "DON";

        // Version number.
        // If Version is left empty, then compile date in the format DD-MM-YY.
        const string Version   = "1.0";
        const string Author    = "Ehsan Rashid";

        const string Months ("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");

    }

    string info (bool uci)
    {
        stringstream sinfo;

        if (uci) sinfo << "id name ";
        sinfo << Engine << ' ';

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

            sinfo << setfill ('0')
                << setw (2) << day << '-'
                << setw (2) << (Months.find (month) / 4 + 1) << '-'
                << setw (2) << year.substr (2);
        }
        else
        {
            sinfo << Version;
        }

#ifdef _64BIT
        sinfo << " x64";
#else
        sinfo << " x86";
#endif

#ifdef POPCNT
        sinfo << " SSE4.2";
#endif

        sinfo << ((uci) ? "\nid author " : " by ");

        sinfo << Author;

        sinfo << endl;

        return sinfo.str ();
    }

    void start (const std::string &args)
    {
        cout << Engine::info () << endl;

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Zobrist  ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Searcher ::initialize ();
        Pawns    ::initialize ();
        Evaluator::initialize ();
        Threads   .initialize ();

        size_t size_mb = TT.resize (int32_t (*(Options["Hash"])));

        cout << "info string " << Threads.size () << " thread(s)." << endl;
        cout << "info string " << size_mb         << " MB Hash."   << endl;

#ifdef _DEBUG
        Tester::main_test ();
        
        //system ("pause");
        //return;

#endif
        //log_io (true);

        UCI   ::start (args);

        //log_io (false);

        Threads.deinitialize ();
        UCI   ::deinitialize ();
    }

    void stop ()
    {
        UCI::stop ();
        Threads.deinitialize ();
    }

    void exit (int32_t code)
    {
        stop ();
        ::exit (code);
    }

}