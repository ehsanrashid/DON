#include "Engine.h"

#include <sstream>
#include <iomanip>
#include <iostream>
#include "BitBoard.h"
#include "Position.h"
#include "UCI.h"
#include "Transposition.h"
#include "Zobrist.h"
#include "Tester.h"

namespace Engine {

    namespace {

        const ::std::string Engine    = "DON";
        
        // Version number. If Version is left empty, then compile date, in the
        // format DD-MM-YY, is shown in engine_info.
        const ::std::string Version   = ""; //"1.1";
        const ::std::string Author    = "Ehsan Rashid";

        const ::std::string Months ("Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec");

    }
    
    ::std::string info (bool uci)
    {
        ::std::stringstream sinfo;

        if (uci) sinfo << "id name ";
        sinfo << Engine << " ";

        if (Version.empty ())
        {
            // From compiler, format is "Sep 2 2013"
            ::std::stringstream date (__DATE__);
            ::std::string
                month,
                day,
                year;

            date 
                >> month
                >> day
                >> year;

            sinfo << ::std::setfill ('0')
                << ::std::setw (2) << day
                << ::std::setw (2) << (Months.find (month) / 4 + 1)
                << ::std::setw (2) << year.substr (2);
        }
        else
        {
            sinfo << Version;
        }

#if defined(_WIN64)

        sinfo << " x64";

#else

        sinfo << " x86";

#endif

#if defined(POPCNT)

        sinfo << " SSE4.2";

#endif

        sinfo << ((uci) ? "\nid author " : " by ");

        sinfo << Author;

        return sinfo.str ();
    }

    void start ()
    {
        ::std::cout << Engine::info () << ::std::endl;

        BitBoard::initialize ();

        //Tester::main_test ();

        UCI::start ();


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