#include "UCI.h"

#include <iostream>
#include <cstdarg>
#include "xstring.h"

#include "Engine.h"
#include "Transposition.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Benchmark.h"
#include "Notation.h"
#include "TriLogger.h"
#include "Thread.h"

namespace UCI {

    using namespace std;
    using namespace Searcher;
    using namespace MoveGenerator;

    typedef istringstream cmdstream;

    namespace {

        // Root position
        Position RootPos (int8_t (0));

        // Keep track of position keys along the setup moves
        // (from start position to the position just before to start searching).
        // Needed by repetition draw detection.
        StateInfoStackPtr SetupStates;

        bool running = false;

#pragma region uci-commands

        void exe_uci ()
        {
            cout
                << Engine::info (true) 
                << Options
                << "uciok" << endl;
        }

        void exe_ucinewgame ()
        {
            if (!bool (*(Options["Never Clear Hash"]))) TT.clear ();
        }

        void exe_isready ()
        {
            cout << "readyok" << endl;
        }

        void exe_setoption (cmdstream &cstm)
        {
            string token;
            // consume "name" token
            if (cstm.good () && (cstm >> token) &&
                iequals (token, "name"))
            {
                string name;
                // Read option-name (can contain spaces)
                // consume "value" token
                while (cstm.good () && (cstm >> token) &&
                    !iequals (token, "value"))
                {
                    name += whitespace (name) ? token : " " + token;
                }

                string value;
                // Read option-value (can contain spaces)
                while (cstm.good () && (cstm >> token))
                {
                    value += whitespace (value) ? token : " " + token;
                }

                if (Options.count (name) > 0)
                {
                    *Options[name] = value;
                }
                else
                {
                    sync_cout << "WHAT??? No such option: \'" << name << "\'" << sync_endl;
                }
            }
        }

        // exe_register() is the command to try to register an engine or to tell the engine that registration
        // will be done later. This command should always be sent if the engine has sent "registration error"
        // at program startup.
        // The following tokens are allowed:
        // * later
        //   the user doesn't want to register the engine now.
        // * name <x>
        //   the engine should be registered with the name <x>
        // * code <y>
        //   the engine should be registered with the code <y>
        // Example:
        //   "register later"
        //   "register name Stefan MK code 4359874324"
        void exe_register (cmdstream &cstm)
        {

        }

        // position(cmd) is called when engine receives the "position" UCI command.
        // The function sets up the position:
        //  - starting position ("startpos")
        //  - fen-string position ("fen")
        // and then makes the moves given in the following move list ("moves")
        // also saving the moves on stack.
        void exe_position (cmdstream &cstm)
        {
            string token;
            string fen;
            // consume "startpos" or "fen" token
            if (cstm.good () && (cstm >> token) &&
                iequals (token, "startpos"))
            {
                fen = FEN_N;
                cstm >> token; // Consume "moves" token if any
            }
            else if (iequals (token, "fen"))
            {
                // consume "moves" token if any
                while (cstm.good () && (cstm >> token) &&
                    !iequals (token, "moves"))
                {
                    fen += whitespace (fen) ? token : " " + token;
                }

                ASSERT (_ok (fen));
            }
            else return;

            Key posi_key = RootPos.posi_key ();

            RootPos.setup (fen, Threads.main (), *(Options["UCI_Chess960"]));

            if (ClearHash && posi_key != RootPos.posi_key ())
            {
                if (!bool (*(Options["Never Clear Hash"]))) TT.clear ();
            }
            ClearHash = false;

            SetupStates = StateInfoStackPtr (new StateInfoStack ());

            // parse and validate game moves (if any)
            if (iequals (token, "moves"))
            {
                while (cstm.good () && (cstm >> token))
                {
                    Move m = move_from_can (token, RootPos);
                    if (MOVE_NONE == m)
                    {
                        sync_cout << "ERROR: Illegal Move" + token << sync_endl;
                        break;
                    }
                    SetupStates->push (StateInfo ());
                    RootPos.do_move (m, SetupStates->top ());
                }
            }
        }

        // exe_go(cmd) is called when engine receives the "go" UCI command.
        // The function sets the thinking time and other parameters:
        //  - wtime and btime
        //  - winc and binc
        //  - movestogo
        //  - movetime
        //  - depth
        //  - nodes
        //  - mate
        //  - infinite
        //  - ponder
        // and starts the search.
        void exe_go (cmdstream &cstm)
        {
            Limits_t limits;
            string  token;
            int32_t value;

            while (cstm.good () && (cstm >> token))
            {
                if      (iequals (token, "wtime"))      cstm >> limits.game_clock[WHITE].time;
                else if (iequals (token, "btime"))      cstm >> limits.game_clock[BLACK].time;
                else if (iequals (token, "winc"))       cstm >> limits.game_clock[WHITE].inc;
                else if (iequals (token, "binc"))       cstm >> limits.game_clock[BLACK].inc;
                else if (iequals (token, "movetime"))   { cstm >> value; limits.move_time   = value; }
                else if (iequals (token, "movestogo"))  { cstm >> value; limits.moves_to_go = value; }
                else if (iequals (token, "depth"))      { cstm >> value; limits.depth       = value; }
                else if (iequals (token, "nodes"))      { cstm >> value; limits.nodes       = value; }
                else if (iequals (token, "mate"))       { cstm >> value; limits.mate_in     = value; }
                else if (iequals (token, "infinite"))   limits.infinite  = true;
                else if (iequals (token, "ponder"))     limits.ponder    = true;
                // parse and validate search moves (if any)
                else if      (iequals (token, "searchmoves"))
                {
                    while (cstm.good () && (cstm >> token))
                    {
                        Move m = move_from_can (token, RootPos);
                        if (MOVE_NONE == m) continue;
                        limits.search_moves.emplace_back (m);
                    }
                }
            }

            Threads.start_thinking (RootPos, limits, SetupStates);
        }

        void exe_ponderhit ()
        {
            Limits.ponder = false;
        }

        void exe_debug (cmdstream &cstm)
        {
            // debug on/off
        }

        void exe_print ()
        {
            cout << RootPos << endl;
        }

        void exe_key ()
        {
            cout << hex << uppercase << setfill ('0')
                << "fen: " << RootPos.fen () << endl
                << "posi key: " << setw (16) << RootPos.posi_key () << endl
                << "matl key: " << setw (16) << RootPos.matl_key () << endl
                << "pawn key: " << setw (16) << RootPos.pawn_key ()
                << dec << endl;
        }

        void exe_allmoves ()
        {

            if (RootPos.checkers ())
            {
                cout << "\nEvasion moves: ";
                for (MoveList<EVASION> itr (RootPos); *itr; ++itr)
                {
                    cout << move_to_san (*itr, RootPos) << " ";
                }
            }
            else
            {
                cout << "\nQuiet moves: ";
                for (MoveList<QUIET> itr (RootPos); *itr; ++itr)
                {
                    cout << move_to_san (*itr, RootPos) << " ";
                }

                cout << "\nCheck moves: ";
                for (MoveList<CHECK> itr (RootPos); *itr; ++itr)
                {
                    cout << move_to_san (*itr, RootPos) << " ";
                }

                cout << "\nQuiet Check moves: ";
                for (MoveList<QUIET_CHECK> itr (RootPos); *itr; ++itr)
                {
                    cout << move_to_san (*itr, RootPos) << " ";
                }
            }

            cout << "\nLegal moves: ";
            for (MoveList<LEGAL> itr (RootPos); *itr; ++itr)
            {
                cout << move_to_san (*itr, RootPos) << " ";
            }

            cout << endl;
        }

        void exe_flip ()
        {
            RootPos.flip ();
        }

        void exe_eval ()
        {
            RootColor = RootPos.active (); // Ensure it is set
            cout << Evaluator::trace (RootPos) << endl;
        }

        void exe_perft (cmdstream &cstm)
        {
            string token;
            // Read perft depth
            if (cstm.good () && (cstm >> token))
            {
                stringstream ss;
                ss  << Options["Hash"] << " "
                    << Options["Threads"] << " "
                    << token << " current perft";

                benchmark (ss, RootPos);
            }
        }

        void exe_stop ()
        {
            Signals.stop = true;
            Threads.main ()->notify_one (); // Could be sleeping
        }

#pragma endregion

    }

    void start (const string &args)
    {
        RootPos.setup (FEN_N, Threads.main (), *(Options["UCI_Chess960"]));

        running = args.empty ();
        string cmd = args;
        string token;
        do
        {
            // Block here waiting for input
            if (running && !getline (cin, cmd, '\n')) cmd = "quit";
            if (whitespace (cmd)) continue;

            try
            {
                cmdstream cstm (cmd);
                cstm >> skipws >> token;

                if      (iequals (token, "uci"))        exe_uci ();
                else if (iequals (token, "ucinewgame")) exe_ucinewgame ();
                else if (iequals (token, "isready"))    exe_isready ();
                else if (iequals (token, "register"))   exe_register (cstm);
                else if (iequals (token, "setoption"))  exe_setoption (cstm);
                else if (iequals (token, "position"))   exe_position (cstm);
                else if (iequals (token, "go"))         exe_go (cstm);
                else if (iequals (token, "ponderhit"))
                {
                    // GUI sends 'ponderhit' to tell us to ponder on the same move the
                    // opponent has played. In case signals.stop_on_ponderhit stream set we are
                    // waiting for 'ponderhit' to stop the search (for instance because we
                    // already ran out of time), otherwise we should continue searching but
                    // switching from pondering to normal search.
                    Signals.stop_on_ponderhit ? exe_stop () : exe_ponderhit ();
                }
                else if (iequals (token, "debug"))      exe_debug (cstm);
                else if (iequals (token, "print"))      exe_print ();
                else if (iequals (token, "key"))        exe_key ();
                else if (iequals (token, "allmoves"))   exe_allmoves ();
                else if (iequals (token, "flip"))       exe_flip ();
                else if (iequals (token, "eval"))       exe_eval ();
                else if (iequals (token, "perft"))      exe_perft (cstm);
                else if (iequals (token, "bench"))      benchmark (cstm, RootPos);
                else if (iequals (token, "stop")
                    ||   iequals (token, "quit"))       exe_stop ();
                else
                {
                    sync_cout << "WHAT??? No such command: \'" << cmd << "\'" << sync_endl;
                }
            }
            catch (exception &exp) // (...)
            {
                sync_cout << exp.what () << sync_endl;
            }
        }
        while (running && !iequals (cmd, "quit"));

    }

    void stop ()
    {
        running = false;
        exe_stop ();
        Threads.wait_for_think_finished (); // Cannot quit while search stream active
    }

    //void send_responce (const char format[], ...)
    //{
    //    try
    //    {
    //        static char buf[1024];
    //        size_t size  =   sizeof (buf);
    //        size_t count = _countof (buf);
    //        memset (buf, 0, size);
    //        va_list args;
    //        va_start (args, format);
    //        int32_t copied = vsnprintf_s (buf, count, _TRUNCATE, format, args);
    //        va_end (args);
    //        if (copied != -1)
    //        {
    //            buf[copied] = '\0';
    //            cout << buf << endl;
    //        }
    //    }
    //    catch (...)
    //    {
    //    }
    //}

}
