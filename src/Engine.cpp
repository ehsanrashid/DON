#include "Engine.h"

#include <sstream>
#include <iomanip>

#include "BitBoard.h"
#include "BitBases.h"
#include "Pawns.h"
#include "Material.h"
#include "Evaluator.h"
#include "Searcher.h"
#include "Transposition.h"
#include "TB_Syzygy.h"
#include "DebugLogger.h"
#include "Thread.h"
#include "UCI.h"
#include "Notation.h"
#include "Tester.h"

namespace Engine {

    using namespace std;

    namespace {

        const string Name      = "DON";

        // Version number.
        // If Version is left empty, then compile date in the format DD-MM-YY.
        const string Version   = "";
        const string Author    = "Ehsan Rashid";

        const string Months ("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");

    }

    string info (bool uci)
    {
        ostringstream os;

        if (uci) os << "id name ";
        os << Name << " ";

#if defined (VERSION)
        os << VERSION << setfill ('0');
#else
        if (Version.empty ())
        {
            // From compiler, format is "Sep 2 2013"
            istringstream is (__DATE__);

            string month
                ,  day
                ,  year;

            is
                >> month
                >> day
                >> year;

            os  << setfill ('0')
                << setw (2) << (day) //<< '-'
                << setw (2) << (Months.find (month) / 4 + 1) //<< '-'
                << setw (2) << (year.substr (2));
        }
        else
        {
            os << Version << setfill ('0');
        }
#endif

#ifdef _64BIT
        os << " x64";
#else
        os << " w32";
#endif

#ifdef POPCNT
        os << "-modern";
#endif

        os  << "\n" 
            << ((uci) ? "id author " : "(c) 2014 ")
            << Author << "\n";

        return os.str ();
    }

    void run (const std::string &args)
    {
        cout << Engine::info (false) << endl;

//        cout << "info string Processor(s) found " << cpu_count () << "." << endl;

#ifdef POPCNT
        cout << "info string POPCNT available." << endl;
#endif

#ifdef LPAGES
        cout << "info string LARGE PAGES available." << endl;
        MemoryHandler::initialize ();
#endif

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Zobrist  ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Searcher ::initialize ();
        Pawns    ::initialize ();
        Evaluator::initialize ();
        Threadpool.initialize ();
        
        TT.resize (int32_t (*(Options["Hash"])), true);

        string syzygy_path = string (*(Options["Syzygy Path"]));
        TBSyzygy::initialize (syzygy_path);

        cout << endl;

#ifndef NDEBUG
        //Tester::main_test ();
        //system ("pause");
        //return;
#endif

        UCI   ::start (args);

    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (int32_t code)
    {
        UCI::stop ();
        
        Threadpool.deinitialize ();
        UCI::deinitialize ();

        ::exit (code);
    }

}