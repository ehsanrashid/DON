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

        const string NAME      = "DON";

        // Version number.
        // If Version is left empty, then compile date in the format DD-MM-YY.
        const string VERSION   = "";
        const string AUTHOR    = "Ehsan Rashid";

        const i08 MAX_MONTH = 12;
        const string MONTHS[MAX_MONTH] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

        int find_month (const string &month)
        {
            for (auto m = 0; m < MAX_MONTH; ++m)
            {
                if (month == MONTHS[m]) return m+1;
            }
            return 0;
        }

    }

    string info (bool uci)
    {
        ostringstream oss;

        if (uci) oss << "id name ";
        oss << NAME << " ";

        oss << setfill ('0');
#if defined (VER)
        oss << VER;
#else
        if (white_spaces (VERSION))
        {
            // From compiler, format is "Sep 2 2013"
            istringstream iss (__DATE__);
            string month, day, year;
            iss >> month >> day >> year;
            oss << setw (2) << (day)
                << setw (2) << (find_month (month))
                << setw (2) << (year.substr (2));
        }
        else
        {
            oss << VERSION;
        }
#endif
        oss << setfill (' ');

#ifdef BIT64
        oss << " x64";
#else
        oss << " w32";
#endif

#ifdef BM2
        oss << "-BM2";
#endif

#ifdef ABM
        oss << "-ABM";
#elif POP
        oss << "-POP";
#endif

#ifdef LPAGES
        oss << "-LP";
#endif

        oss << (uci ? "\nid author " : " by ") << AUTHOR << "\n";

        return oss.str ();
    }

    void run (const string &arg)
    {
        cout << info (false) << endl;

#ifdef LPAGES
        Memory::initialize ();
#endif

        UCI      ::initialize ();
        BitBoard ::initialize ();
        Position ::initialize ();
        BitBases ::initialize ();
        Pawns    ::initialize ();
        Evaluator::initialize ();
        EndGame  ::initialize ();
        Threadpool.initialize ();
        Searcher ::initialize ();

        TT.auto_size (i32(Options["Hash"]), true);

        cout << endl;

        UCI::start (arg);
    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (int code)
    {
        UCI::stop ();

        Threadpool.exit ();
        EndGame::exit ();
        UCI::exit ();

        ::exit (code);
    }

}