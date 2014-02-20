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
#include "UCI.h"
#include "DebugLogger.h"
#include "Thread.h"

#ifndef NDEBUG
#   include "Tester.h"
#endif

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

//        cout 
//            << "info string " << cpu_count () << " processor(s) found."
//#ifdef POPCNT
//            << " POPCNT available."
//#endif
//            << endl;

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

#ifndef NDEBUG
        //string f = "r1bqk2r/pppp1ppp/8/8/8/5N2/P1P1QPPP/q1B1KB1R b Kq - 0 1";
        //Position p(f);
        //cout << p;

        //StateInfo si;
        //Move m = mk_move(SQ_H8, SQ_F8);
        //cout << p.pseudo_legal (m) << endl;
        //cout << p.legal (m) << endl;
        //p.do_move (m, si);
        ////BitBoard::print(8589934592);
        //cout << p;
        ////Tester::main_test ();
        //system ("pause");
        //return;
#endif

        UCI   ::start (args);

    }

    // Exit from engine with exit code. (in case of some crash)
    void exit (int32_t code)
    {
        UCI   ::stop ();
        if (Searcher::Book.is_open ()) Searcher::Book.close ();
        Threads.deinitialize ();
        UCI   ::deinitialize ();

        ::exit (code);
    }

}