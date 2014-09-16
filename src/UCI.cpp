#include "UCI.h"

#include <cstdarg>

#include "Engine.h"
#include "Benchmark.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Thread.h"
#include "Transposition.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;

namespace UCI {

    using namespace Search;
    using namespace MoveGen;
    using namespace Threads;
    using namespace Notation;

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

        inline void exe_isready ()
        {
            sync_cout << "readyok" << sync_endl;
        }

        inline void exe_setoption (cmdstream &cmds)
        {
            string token;
            if (cmds >> token)
            {
                // consume "name" token
                if (token == "name")
                {
                    string name;
                    // Read option-name (can contain spaces)
                    // consume "value" token
                    while (cmds >> token && token != "value")
                    {
                        name += string (" ", !name.empty ()) + token;
                    }

                    string value;
                    // Read option-value (can contain spaces)
                    while (cmds >> token)
                    {
                        value += string (" ", !value.empty ()) + token;
                    }

                    if (Options.count (name) > 0)
                    {
                        Options[name] = value;
                    }
                    else
                    {
                        sync_cout << "WHAT??? No such option: \'" << name << "\'" << sync_endl;
                    }
                }
            }
        }

        // TODO::
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
        inline void exe_register (cmdstream &cmds)
        {
            string token;

            if (cmds >> token)
            {
                if (token == "name")
                {
                    string name;
                    // Read name (can contain spaces)
                    // consume "value" token
                    while (cmds >> token && token != "code")
                    {
                        name += string (" ", !name.empty ()) + token;
                    }

                    string code;
                    // Read code (can contain spaces)
                    while (cmds >> token)
                    {
                        code += string (" ", !code.empty ()) + token;
                    }
                    //cout << name << "\n" << code << endl;
                }
                else
                if (token == "later")
                {

                }
            }
        }

        // exe_position(cmd) is called when engine receives the "position" UCI command.
        // The function sets up the position:
        //  - starting position ("startpos")
        //  - fen-string position ("fen")
        // and then makes the moves given in the following move list ("moves")
        // also saving the moves on stack.
        inline void exe_position (cmdstream &cmds)
        {
            string token;
            string fen = "";
            if (cmds >> token)
            {
                if (token == "startpos")    // Consume "startpos" or "fen" token
                {
                    fen = STARTUP_FEN;
                    cmds >> token;          // Consume "moves" token if any
                }
                else
                {
                    if (token != "fen") fen += token;
                    while (cmds >> token && token != "moves") // Consume "moves" token if any
                    {
                        fen += string (" ", !fen.empty ()) + token;
                    }
                    //ASSERT (_ok (fen));
                    //if (!_ok (fen)) return;
                }
            }
            else return;
            
            RootPos.setup (fen, Threadpool.main (), bool(Options["UCI_Chess960"]));
            
            SetupStates = StateInfoStackPtr (new StateInfoStack ());
            
            if (token == "moves")
            {
                while (cmds >> token)   // Parse and validate game moves (if any)
                {
                    Move m = move_from_can (token, RootPos);
                    if (MOVE_NONE == m)
                    {
                        cerr << "ERROR: illegal Move '" + token << "'" << endl;
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
        // Starts the search.
        inline void exe_go (cmdstream &cmds)
        {
            LimitsT limits;

            string  token;
            i64     value;
            while (cmds >> token)
            {
                if      (token == "wtime")      { cmds >> value; limits.gameclock[WHITE].time = u32(value >= 0 ? +value : -value); }
                else if (token == "btime")      { cmds >> value; limits.gameclock[BLACK].time = u32(value >= 0 ? +value : -value); }
                else if (token == "winc")       { cmds >> value; limits.gameclock[WHITE].inc  = u32(value >= 0 ? +value : -value); }
                else if (token == "binc")       { cmds >> value; limits.gameclock[BLACK].inc  = u32(value >= 0 ? +value : -value); }
                else if (token == "movetime")   { cmds >> value; limits.movetime  = u32(value >= 0 ? +value : -value); }
                else if (token == "movestogo")  { cmds >> value; limits.movestogo = u08(value >= 0 ? +value : -value); }
                else if (token == "depth")      { cmds >> value; limits.depth     = u08(value >= 0 ? +value : -value); }
                else if (token == "nodes")      { cmds >> value; limits.nodes     = u64(value >= 0 ? +value : -value); }
                else if (token == "mate")       { cmds >> value; limits.mate      = u08(value >= 0 ? +value : -value); }
                else if (token == "infinite")   { limits.infinite  = true; }
                else if (token == "ponder")     { limits.ponder    = true; }
                // parse and validate search moves (if any)
                else if (token == "searchmoves")
                {
                    while (cmds >> token)
                    {
                        Move m = move_from_can (token, RootPos);
                        if (MOVE_NONE != m)
                        {
                            limits.root_moves.push_back (m);
                        }
                    }
                }
            }

            Threadpool.start_thinking (RootPos, limits, SetupStates);
        }

        inline void exe_ponderhit ()
        {
            Limits.ponder = false;
        }

        inline void exe_debug (cmdstream &cmds)
        {
            string token;
            if (cmds >> token)
            {
                if (token == "on")  Debug::log_debug (true);
                else
                if (token == "off") Debug::log_debug (false);
            }
        }
        // Print the root position
        inline void exe_show ()
        {
            sync_cout << RootPos << sync_endl;
        }

        inline void exe_keys ()
        {
            sync_cout
                << hex << uppercase << setfill ('0')
                << "Fen: "                   << RootPos.fen ()      << "\n"
                << "Posi key: " << setw (16) << RootPos.posi_key () << "\n"
                << "Matl key: " << setw (16) << RootPos.matl_key () << "\n"
                << "Pawn key: " << setw (16) << RootPos.pawn_key ()
                << dec << nouppercase << setfill (' ')
                << sync_endl;
        }

        inline void exe_moves ()
        {
            sync_cout;

            if (RootPos.checkers () != U64(0))
            {
                cout << "\nEvasion moves: ";
                for (MoveList<EVASION> ms (RootPos); *ms != MOVE_NONE; ++ms)
                {
                    Move m = *ms;
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }
            }
            else
            {
                cout << "\nQuiet moves: ";
                for (MoveList<QUIET> ms (RootPos); *ms != MOVE_NONE; ++ms)
                {
                    Move m = *ms;
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nCheck moves: ";
                for (MoveList<CHECK> ms (RootPos); *ms != MOVE_NONE; ++ms)
                {
                    Move m = *ms;
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nQuiet Check moves: ";
                for (MoveList<QUIET_CHECK> ms (RootPos); *ms != MOVE_NONE; ++ms)
                {
                    Move m = *ms;
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nCapture moves: ";
                for (MoveList<CAPTURE> ms (RootPos); *ms != MOVE_NONE; ++ms)
                {
                    Move m = *ms;
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }
            }

            cout << "\nLegal moves: ";
            for (MoveList<LEGAL> ms (RootPos); *ms != MOVE_NONE; ++ms)
            {
                Move m = *ms;
                cout << move_to_san (m, RootPos) << " ";
            }

            cout << sync_endl;
        }

        inline void exe_flip ()
        {
            RootPos.flip ();
        }

        inline void exe_eval ()
        {
            sync_cout << Evaluate::trace (RootPos) << sync_endl;
        }

        inline void exe_perft (cmdstream &cmds)
        {
            i32 depth;
            // Read perft 'depth'
            if (cmds >> depth)
            {
                stringstream ss;
                string fen_fn;
                ss  << i32(Options["Hash"])    << " "
                    << i32(Options["Threads"]) << " "
                    << depth << " perft " << ((cmds >> fen_fn) ? fen_fn : "");

                benchmark (ss, RootPos);
            }
        }

        inline void exe_bench (cmdstream &cmds)
        {
            benchmark (cmds, RootPos);
        }
        // Stops the search
        inline void exe_stop ()
        {
            Signals.force_stop = true;
            Threadpool.main ()->notify_one (); // Could be sleeping
        }

    }

    // Wait for a command from the user, parse this text string as an UCI command,
    // and call the appropriate functions. Also intercepts EOF from stdin to ensure
    // that exit gracefully if the GUI dies unexpectedly. In addition to the UCI
    // commands, the function also supports a few debug commands.
    void start (const string &arg)
    {
        RootPos.setup (STARTUP_FEN, Threadpool.main ());

        bool running = arg.empty ();
        string cmd   = arg;
        string token;
        do
        {
            // Block here waiting for input
            if (running && !getline (cin, cmd, '\n')) cmd = "quit";

            cmdstream cmds (cmd);
            cmds >> skipws >> token;

            if      (token.empty ())        continue;
            if      (token == "uci")        exe_uci ();
            else if (token == "ucinewgame") { /*TT.clear ();*/ } // Obsolete command
            else if (token == "isready")    exe_isready ();
            else if (token == "register")   exe_register (cmds);
            else if (token == "setoption")  exe_setoption (cmds);
            else if (token == "position")   exe_position (cmds);
            else if (token == "go")         exe_go (cmds);
            else if (token == "ponderhit")
            {
                // GUI sends 'ponderhit' to tell us to ponder on the same move the
                // opponent has played. In case Signals.ponderhit_stop stream set are
                // waiting for 'ponderhit' to stop the search (for instance because
                // already ran out of time), otherwise should continue searching but
                // switching from pondering to normal search.
                Signals.ponderhit_stop ? exe_stop () : exe_ponderhit ();
            }
            else if (token == "stop"
                  || token == "quit")       exe_stop ();
            else if (token == "debug")      exe_debug (cmds);
            else if (token == "show")       exe_show ();
            else if (token == "keys")       exe_keys ();
            else if (token == "moves")      exe_moves ();
            else if (token == "flip")       exe_flip ();
            else if (token == "eval")       exe_eval ();
            else if (token == "perft")      exe_perft (cmds);
            else if (token == "bench")      exe_bench (cmds);
            else if (token == "cls")        system ("cls");
            else
            {
                sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl;
            }

        }
        while (running && cmd != "quit");
    }

    void stop ()
    {
        // Send stop command
        exe_stop ();
        // Cannot quit while search stream active
        Threadpool.wait_for_think_finished ();
        // Close book if open
        Book.close ();
        // Close log file
        Debug::log_debug (false);
    }

}
