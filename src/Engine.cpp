#include "Engine.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include "BitBoard.h"
#include "Zobrist.h"
#include "Pawns.h"
#include "Position.h"
#include "UCI.h"
#include "Tester.h"

namespace Engine {

    using namespace std;

    namespace {

        const string Engine    = "DON";

        // Version number. If Version is left empty, then compile date, in the
        // format DD-MM-YY, is shown in engine_info.
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

        return sinfo.str ();
    }

    void start ()
    {

        BitBoard::initialize ();
        Zobrist ::initialize ();
        Pawns   ::initialize ();
        Position::initialize ();

        cout << Engine::info () << endl;

#ifdef _DEBUG

        Tester::main_test ();

#endif

        //UCI::start ();


    }

    void stop ()
    {
        UCI::stop ();
    }

    void exit (int32_t exit_code)
    {
        stop ();
        ::exit (exit_code);
    }


}