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
#include "Thread.h"

namespace Engine {

    using namespace std;

    namespace {

        // Version number. If Version is left empty, then show compile date in the format DD-MM-YY.
        const string VERSION   = "";

        const i08 MAX_MONTH = 12;
        const string MONTHS[MAX_MONTH] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

        i32 month_index (const string &month)
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
        oss << "DON ";

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
                << setw (2) << (month_index (month))
                << setw (2) << (year.substr (2));
        }
        else
        {
            oss << VERSION;
        }
#endif
        oss << setfill (' ');

#ifdef BIT64
        oss << ".64";
#else
        oss << ".32";
#endif

#ifdef BM2
        oss << ".BM2";
#elif ABM
        oss << ".ABM";
#elif POP
        oss << ".POP";
#endif

#ifdef LPAGES
        oss << ".LP";
#endif

        oss << (uci ? "\nid author " : " by ") << "Ehsan Rashid";

        return oss.str ();
    }

    void run (const string &arg)
    {
        std::cout << info (false) << std::endl;

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

        std::cout << std::endl;

        UCI::loop (arg);
    }

    void stop (i32 code)
    {
        Threadpool.deinitialize ();
        EndGame  ::deinitialize ();
        UCI      ::deinitialize ();
#ifdef LPAGES
        Memory   ::deinitialize ();
#endif
        ::exit (code);
    }

}