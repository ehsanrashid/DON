#include "Engine.h"

#include <sstream>
#include <iomanip>

#include "UCI.h"
#include "BitBases.h"
#include "Pawns.h"
#include "Material.h"
#include "Evaluator.h"
#include "Searcher.h"
#include "Transposition.h"
#include "Debugger.h"
#include "Thread.h"
#include "Notation.h"

namespace Engine {

    using namespace std;

    namespace {

        const string Name      = "DON";

        // Version number.
        // If Version is left empty, then compile date in the format DD-MM-YY.
        const string Version   = "";
        const string Author    = "Ehsan Rashid";

        const i08 MONTHS = 12;
        const string Months[MONTHS] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

        i16 find_month (const string &month)
        { 
            for (i08 m = 0; m < MONTHS; ++m)
            { 
                if (month == Months[m]) return m+1;
            }
            return 0;
        }

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
                << setw (2) << (find_month (month)) //<< '-'
                << setw (2) << (year.substr (2))
                << setfill (' ');
        }
        else
        {
            oss << Version;
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

        oss << "\n";
        if (uci) oss << "id author " << Author;
        else     oss << Author << " (c) 2014";
        oss << "\n";

        return oss.str ();
    }

    void run (const std::string &arg)
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
        Memory::initialize ();
#endif

        //cout << "info string Processor(s) found " << cpu_count () << "." << endl;

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Zobrist  ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Search ::initialize ();
        Pawns    ::initialize ();
        Evaluate ::initialize ();
        EndGame  ::initialize ();
        Threadpool.initialize ();
        
        TT.auto_size (i32(Options["Hash"]), true);

        cout << endl;

        UCI      ::start (arg);

    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (i32 code)
    {
        UCI      ::stop ();
        
        Threadpool.deinitialize ();
        EndGame  ::deinitialize ();
        UCI      ::deinitialize ();

        ::exit (code);
    }

}