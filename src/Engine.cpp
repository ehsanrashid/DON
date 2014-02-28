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
        ostringstream ss;

        if (uci) ss << "id name ";
        ss << Name << " ";

        if (Version.empty ())
        {
            // From compiler, format is "Sep 2 2013"
            istringstream sdate (__DATE__);

            string month
                ,  day
                ,  year;

            sdate
                >> month
                >> day
                >> year;

            ss  << setfill ('0')
                << setw (2) << (day) //<< '-'
                << setw (2) << (Months.find (month) / 4 + 1) //<< '-'
                << setw (2) << (year.substr (2));
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

#ifdef POPCNT

        ss << "-modern";

#endif

        ss  << "\n" 
            << ((uci) ? "id author " : "(c) 2014 ")
            << Author << "\n";

        return ss.str ();
    }

    void run (const std::string &args)
    {
        cout << Engine::info (false) << endl;

//        cout << "info string Processor(s) found " << cpu_count () << ".\n";

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
        Threads   .initialize ();

        TT.resize (int32_t (*(Options["Hash"])), true);

#ifndef _MSC_VER

        Tablebases::initialize (*(Options["Syzygy Path"]));

#endif

        cout
            << "info string Thread(s) count " << Threads.size () << ".\n"
            << "info string Hash size " << TT.size () << " MB."  << endl;


        UCI   ::start (args);

    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (int32_t code)
    {

        UCI   ::stop ();
        
        if (Searcher::Book.is_open ())
        {
            Searcher::Book.close ();
        }

        Threads.deinitialize ();
        UCI   ::deinitialize ();

        ::exit (code);

    }

}