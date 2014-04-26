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
#include "Debugger.h"
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
        ostringstream oss;

        if (uci) oss << "id name ";
        oss << Name << " ";

#if defined (VERSION)
        oss << VERSION << setfill ('0');
#else
        if (Version.empty ())
        {
            // From compiler, format is "Sep 2 2013"
            istringstream iss (__DATE__);

            string month
                ,  day
                ,  year;

            iss >> month
                >> day
                >> year;

            oss << setfill ('0')
                << setw (2) << (day) //<< '-'
                << setw (2) << (Months.find (month) / 4 + 1) //<< '-'
                << setw (2) << (year.substr (2));
        }
        else
        {
            oss << Version << setfill ('0');
        }
#endif

#ifdef _64BIT
        oss << " x64";
#else
        oss << " w32";
#endif

#ifdef BM2
        oss << "-BM2";
#endif
#ifdef ABM
        oss << "-ABM";
#endif
#ifdef LPAGES
        oss << "-LP";
#endif

        oss << "\n" 
            << ((uci) ? ("id author " + Author)
            : (Author + " (c) 2014")) << "\n";

        return oss.str ();
    }

    void run (const std::string &args)
    {
        cout << Engine::info (false) << endl;

#ifdef ABM
        //cout << "info string ABM available." << endl;
#endif
#ifdef BM2
        //cout << "info string BM2 available." << endl;
#endif
#ifdef LPAGES
        //cout << "info string LARGE PAGES available." << endl;
        MemoryHandler::initialize ();
#endif

        cout << "info string Processor(s) found " << cpu_count () << "." << endl;

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Zobrist  ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Searcher ::initialize ();
        Pawns    ::initialize ();
        Evaluator::initialize ();
        EndGame  ::initialize ();
        Threadpool.initialize ();
        
        TT.resize (i32 (Options["Hash"]), true);

        cout << endl;

        UCI      ::start (args);

    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (i32 code)
    {
        UCI       ::stop ();
        
        Threadpool.deinitialize ();
        EndGame  ::deinitialize ();
        UCI      ::deinitialize ();

        ::exit (code);
    }

}