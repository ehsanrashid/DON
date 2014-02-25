#include "UCI.h"

#include <iostream>
#include <cstdarg>

#include "Engine.h"
#include "Transposition.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Benchmark.h"
#include "Notation.h"
#include "Thread.h"

namespace UCI {

    using namespace std;
    using namespace MoveGenerator;

    typedef istringstream cmdstream;

    namespace {

        // Root position
        Position RootPos (0);

        // Keep track of position keys along the setup moves
        // (from start position to the position just before to start searching).
        // Needed by repetition draw detection.
        StateInfoStackPtr SetupStates;

        inline void exe_uci ()
        {
            sync_cout
                << Engine::info () 
                << Options
                << "uciok"
                << sync_endl;
        }

        inline void exe_ucinewgame ()
        {
            TT.clear_hash = !bool (*(Options["Never Clear Hash"]));
        }

        inline void exe_isready ()
        {
            sync_cout << "readyok" << sync_endl;
        }

        inline void exe_setoption (cmdstream &cstm)
        {
            string token;
            if (cstm >> token)
            {
                // consume "name" token
                if (token == "name")
                {
                    string name;
                    // Read option-name (can contain spaces)
                    // consume "value" token
                    while (cstm >> token && token != "value")
                    {
                        name += string (" ", !name.empty ()) + token;
                    }

                    string value;
                    // Read option-value (can contain spaces)
                    while (cstm >> token)
                    {
                        value += string (" ", !value.empty ()) + token;
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
        inline void exe_register (cmdstream &cstm)
        {
            string token;

            if (cstm >> token)
            {
                if (token == "name")
                {
                    string name;
                    // Read name (can contain spaces)
                    // consume "value" token
                    while (cstm >> token && token != "code")
                    {
                        name += string (" ", !name.empty ()) + token;
                    }

                    string code;
                    // Read code (can contain spaces)
                    while (cstm >> token)
                    {
                        code += string (" ", !code.empty ()) + token;
                    }

                    // TODO::
                    cout << name << "\n" << code << endl;
                }
                else if (token == "later")
                {
                    // TODO::

                }
            }
        }

        // position(cmd) is called when engine receives the "position" UCI command.
        // The function sets up the position:
        //  - starting position ("startpos")
        //  - fen-string position ("fen")
        // and then makes the moves given in the following move list ("moves")
        // also saving the moves on stack.
        inline void exe_position (cmdstream &cstm)
        {
            string token;
            string fen;
            if (cstm >> token)
            {
                // consume "startpos" or "fen" token
                if      (token == "startpos")
                {
                    fen = FEN_N;
                    cstm >> token; // Consume "moves" token if any
                }
                else if (token == "fen")
                {
                    // consume "moves" token if any
                    while (cstm >> token && token != "moves")
                    {
                        fen += string (" ", !fen.empty ()) + token;
                    }

                    //ASSERT (_ok (fen));
                    //if (!_ok (fen)) return;
                }
                else return;
            }
            else return;
            
            Key posi_key = RootPos.posi_key ();

            RootPos.setup (fen, Threads.main (), bool (*(Options["UCI_Chess960"])));
            
            if (posi_key != RootPos.posi_key ())
            {
                TT.clear ();
            }

            SetupStates = StateInfoStackPtr (new StateInfoStack ());

            // parse and validate game moves (if any)
            if (token == "moves")
            {
                while (cstm >> token)
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
        inline void exe_go (cmdstream &cstm)
        {
            Searcher::LimitsT limits;
            string  token;
            int32_t value;

            while (cstm >> token)
            {
                if      (token == "wtime")      cstm >> limits.game_clock[WHITE].time;
                else if (token == "btime")      cstm >> limits.game_clock[BLACK].time;
                else if (token == "winc")       cstm >> limits.game_clock[WHITE].inc;
                else if (token == "binc")       cstm >> limits.game_clock[BLACK].inc;
                else if (token == "movetime")   { cstm >> value; limits.move_time   = value; }
                else if (token == "movestogo")  { cstm >> value; limits.moves_to_go = value; }
                else if (token == "depth")      { cstm >> value; limits.depth       = value; }
                else if (token == "nodes")      { cstm >> value; limits.nodes       = value; }
                else if (token == "mate")       { cstm >> value; limits.mate_in     = value; }
                else if (token == "infinite")   limits.infinite  = true;
                else if (token == "ponder")     limits.ponder    = true;
                // parse and validate search moves (if any)
                else if      (token == "searchmoves")
                {
                    while (cstm >> token)
                    {
                        Move m = move_from_can (token, RootPos);
                        
                        if (MOVE_NONE == m) continue;
                        
                        limits.search_moves.push_back (m);
                    }
                }
            }

            Threads.start_thinking (RootPos, limits, SetupStates);
        }

        inline void exe_ponderhit ()
        {
            Searcher::Limits.ponder = false;
        }

        inline void exe_debug (cmdstream &cstm)
        {
            (void) cstm;
            // debug on/off
        }

        inline void exe_print ()
        {
            sync_cout << RootPos << sync_endl;
        }

        inline void exe_key ()
        {
            sync_cout
                << hex << uppercase << setfill ('0')
                << "fen: " << RootPos.fen () << "\n"
                << "posi key: " << setw (16) << RootPos.posi_key () << "\n"
                << "matl key: " << setw (16) << RootPos.matl_key () << "\n"
                << "pawn key: " << setw (16) << RootPos.pawn_key ()
                << dec << sync_endl;
        }

        inline void exe_allmoves ()
        {
            sync_cout;

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

                cout << "\nCapture moves: ";
                for (MoveList<CAPTURE> itr (RootPos); *itr; ++itr)
                {
                    cout << move_to_san (*itr, RootPos) << " ";
                }

            }

            cout << "\nLegal moves: ";
            for (MoveList<LEGAL> itr (RootPos); *itr; ++itr)
            {
                cout << move_to_san (*itr, RootPos) << " ";
            }

            cout << sync_endl;
        }

        inline void exe_flip ()
        {
            RootPos.flip ();
        }

        inline void exe_eval ()
        {
            Searcher::RootColor = RootPos.active (); // Ensure it is set
            sync_cout << Evaluator::trace (RootPos) << sync_endl;
        }

        inline void exe_perft (cmdstream &cstm)
        {
            string token;
            // Read perft depth
            if (cstm >> token)
            {
                stringstream ss;
                ss  << *(Options["Hash"])    << " "
                    << *(Options["Threads"]) << " "
                    << token << " current perft";

                benchmark (ss, RootPos);
            }
        }

        inline void exe_stop ()
        {
            Searcher::Signals.stop = true;
            Threads.main ()->notify_one (); // Could be sleeping
        }

    }

    void start (const string &args)
    {
        RootPos.setup (FEN_N, Threads.main (), bool (*(Options["UCI_Chess960"])));

        bool running = args.empty ();
        string cmd = args;
        string token;
        do
        {
            // Block here waiting for input
            if (running && !getline (cin, cmd, '\n')) cmd = "quit";

            cmdstream cstm (cmd);
            cstm >> skipws >> token;

            if (token.empty ())             continue;
            else if (token == "uci")        exe_uci ();
            else if (token == "ucinewgame") exe_ucinewgame ();
            else if (token == "isready")    exe_isready ();
            else if (token == "register")   exe_register (cstm);
            else if (token == "setoption")  exe_setoption (cstm);
            else if (token == "position")   exe_position (cstm);
            else if (token == "go")         exe_go (cstm);
            else if (token == "ponderhit")
            {
                // GUI sends 'ponderhit' to tell us to ponder on the same move the
                // opponent has played. In case signals.stop_on_ponderhit stream set we are
                // waiting for 'ponderhit' to stop the search (for instance because we
                // already ran out of time), otherwise we should continue searching but
                // switching from pondering to normal search.
                Searcher::Signals.stop_on_ponderhit ? exe_stop () : exe_ponderhit ();
            }
            else if (token == "debug")      exe_debug (cstm);
            else if (token == "print")      exe_print ();
            else if (token == "key")        exe_key ();
            else if (token == "allmoves")   exe_allmoves ();
            else if (token == "flip")       exe_flip ();
            else if (token == "eval")       exe_eval ();
            else if (token == "perft")      exe_perft (cstm);
            else if (token == "bench")      benchmark (cstm, RootPos);
            else if (token == "stop"
                ||   token == "quit")       exe_stop ();
            else
            {
                sync_cout << "WHAT??? No such command: \'" << cmd << "\'" << sync_endl;
            }

        }
        while (running && cmd != "quit");

    }

    void stop ()
    {
        exe_stop ();
        Threads.wait_for_think_finished (); // Cannot quit while search stream active
    }

}
