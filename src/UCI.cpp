#include "UCI.h"

#include <cstdarg>

#include "Engine.h"
#include "Benchmark.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Transposition.h"
#include "Notation.h"
#include "Debugger.h"

using namespace std;

namespace UCI {

    using namespace Searcher;
    using namespace MoveGen;
    using namespace Notation;

    typedef istringstream cmdstream;

    namespace {

        // Root position
        Position RootPos;

        // Stack to keep track of the position states along the setup moves
        // (from the start position to the position just before the search starts).
        // Needed by 'draw by repetition' detection.
        StateStackPtr SetupStates;


        void exe_uci ()
        {
            sync_cout
                << Engine::info () 
                << Options
                << "uciok"
                << sync_endl;
        }
        
        void exe_ucinewgame ()
        {
            reset ();
            TimeMgr.available_nodes = 0;
        }

        void exe_isready ()
        {
            sync_cout << "readyok" << sync_endl;
        }

        void exe_setoption (cmdstream &cmds)
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
                        name += string (" ", !white_spaces (name)) + token;
                    }

                    string value;
                    // Read option-value (can contain spaces)
                    while (cmds >> token)
                    {
                        value += string (" ", !white_spaces (value)) + token;
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
        void exe_register (cmdstream &cmds)
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
                        name += string (" ", !white_spaces (name)) + token;
                    }

                    string code;
                    // Read code (can contain spaces)
                    while (cmds >> token)
                    {
                        code += string (" ", !white_spaces (code)) + token;
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
        void exe_position (cmdstream &cmds)
        {
            string token;
            string fen;

            cmds >> token;  // Consume "startpos" or "fen" token
            if (token == "startpos")
            {
                fen = STARTUP_FEN;
                cmds >> token;          // Consume "moves" token if any
            }
            else
            if (token == "fen")
            {
                while (cmds >> token && token != "moves") // Consume "moves" token if any
                {
                    fen += token + " ";
                }
                
                assert (_ok (fen, Chess960, true));
            }
            else
                return;

            RootPos.setup (fen, Threadpool.main (), Chess960);

            SetupStates = StateStackPtr (new StateStack);

            if (token == "moves")
            {
                while (cmds >> token)   // Parse and validate game moves (if any)
                {
                    auto m = move_from_can (token, RootPos);
                    if (MOVE_NONE == m)
                    {
                        cerr << "ERROR: Illegal Move '" + token << "'" << endl;
                        break;
                    }

                    SetupStates->push (StateInfo ());
                    RootPos.do_move (m, SetupStates->top (), RootPos.gives_check (m, CheckInfo (RootPos)));
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
        // Then starts the search.
        void exe_go (cmdstream &cmds)
        {
            LimitsT limits;

            string  token;
            i64     value;
            while (cmds >> token)
            {
                if      (token == "wtime")      { cmds >> value; limits.clock[WHITE].time = u32(abs (value)); }
                else if (token == "btime")      { cmds >> value; limits.clock[BLACK].time = u32(abs (value)); }
                else if (token == "winc")       { cmds >> value; limits.clock[WHITE].inc  = u32(abs (value)); }
                else if (token == "binc")       { cmds >> value; limits.clock[BLACK].inc  = u32(abs (value)); }
                else if (token == "movetime")   { cmds >> value; limits.movetime  = u32(abs (value)); }
                else if (token == "movestogo")  { cmds >> value; limits.movestogo = u08(abs (value)); }
                else if (token == "depth")      { cmds >> value; limits.depth     = u08(abs (value)); }
                else if (token == "nodes")      { cmds >> value; limits.nodes     = u64(abs (value)); }
                else if (token == "mate")       { cmds >> value; limits.mate      = u08(abs (value)); }
                else if (token == "infinite")   { limits.infinite  = true; }
                else if (token == "ponder")     { limits.ponder    = true; }
                // parse and validate search moves (if any)
                else if (token == "searchmoves")
                {
                    while (cmds >> token)
                    {
                        auto m = move_from_can (token, RootPos);
                        if (MOVE_NONE != m)
                        {
                            limits.root_moves.push_back (m);
                        }
                    }
                }
            }

            Threadpool.start_main (RootPos, limits, SetupStates);
        }

        void exe_ponderhit ()
        {
            Limits.ponder = false;
        }

        void exe_debug (cmdstream &cmds)
        {
            string token;
            if (cmds >> token)
            {
                if (token == "on")  Debugger::log_debug (true);
                else
                if (token == "off") Debugger::log_debug (false);
            }
        }
        // Print the root position
        void exe_show ()
        {
            sync_cout << RootPos << sync_endl;
        }
        // Print the root fen and keys
        void exe_keys ()
        {
            sync_cout
                << hex << uppercase << setfill ('0')
                << "Fen: "                   << RootPos.fen ()      << "\n"
                << "Posi key: " << setw (16) << RootPos.posi_key () << "\n"
                << "Matl key: " << setw (16) << RootPos.matl_key () << "\n"
                << "Pawn key: " << setw (16) << RootPos.pawn_key ()
                << setfill (' ') << nouppercase << dec
                << sync_endl;
        }

        void exe_moves ()
        {
            sync_cout;

            if (RootPos.checkers () != U64(0))
            {
                cout << "\nEvasion moves: ";
                for (const auto &m : MoveList<EVASION> (RootPos))
                {
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }
            }
            else
            {
                cout << "\nQuiet moves: ";
                for (const auto &m : MoveList<QUIET> (RootPos))
                {
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nCheck moves: ";
                for (const auto &m : MoveList<CHECK> (RootPos))
                {
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nQuiet Check moves: ";
                for (const auto &m : MoveList<QUIET_CHECK> (RootPos))
                {
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }

                cout << "\nCapture moves: ";
                for (const auto &m : MoveList<CAPTURE> (RootPos))
                {
                    if (RootPos.legal (m))
                    {
                        cout << move_to_san (m, RootPos) << " ";
                    }
                }
            }

            cout << "\nLegal moves: ";
            for (const auto &m : MoveList<LEGAL> (RootPos))
            {
                cout << move_to_san (m, RootPos) << " ";
            }

            cout << sync_endl;
        }

        void exe_flip ()
        {
            RootPos.flip ();
        }

        void exe_eval ()
        {
            sync_cout << Evaluator::trace (RootPos) << sync_endl;
        }

        void exe_perft (cmdstream &cmds)
        {
            stringstream ss;
            i32    depth;
            depth  = ((cmds >> depth) ? depth : 1);
            string fen_fn;
            fen_fn = ((cmds >> fen_fn) ? fen_fn : "");
            ss  << i32(Options["Hash"])    << " "
                << i32(Options["Threads"]) << " "
                << depth << " perft " << fen_fn;

            benchmark (ss, RootPos);
        }

        // Stops the search
        void exe_stop ()
        {
            Signals.force_stop = true;
            Threadpool.main ()->notify_one (); // Could be sleeping
        }

    }

    // start() waits for a command from stdin, parses it and calls the appropriate
    // function. Also intercepts EOF from stdin to ensure gracefully exiting if the
    // GUI dies unexpectedly. When called with some command line arguments, e.g. to
    // run 'bench', once the command is executed the function returns immediately.
    // In addition to the UCI ones, also some additional debug commands are supported.
    void start (const string &arg)
    {
        RootPos.setup (STARTUP_FEN, Threadpool.main (), Chess960);

        bool running = white_spaces (arg);
        string cmd   = arg;
        string token;
        do
        {
            // Block here waiting for input or EOF
            if (running && !getline (cin, cmd, '\n')) cmd = "quit";

            cmdstream cmds (cmd);
            token.clear ();
            cmds >> skipws >> token;

            if      (white_spaces (token)) continue;
            if      (token == "uci")        exe_uci ();
            else if (token == "ucinewgame") exe_ucinewgame ();
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
            else if (token == "bench")      benchmark (cmds, RootPos);
            else if (token == "autotune")   auto_tune (cmds);
            //else if (!white_spaces (token)) system (token.c_str ());
            else
            {
                sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl;
            }

        } while (running && cmd != "quit");

        Threadpool.main ()->join (); // Cannot quit whilst the search is running
    }

    // stop() stops all the threads and other stuff.
    void stop ()
    {
        // Send stop command
        exe_stop ();
        // Cannot quit while search stream active
        Threadpool.main ()->join ();
        // Close log file
        Debugger::log_debug (false);
    }

}
