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
    using namespace Debugger;

    namespace {

        // Root position
        Position RootPos;

        // Stack to keep track of the position states along the setup moves
        // (from the start position to the position just before the search starts).
        // Needed by 'draw by repetition' detection.
        StateStackPtr SetupStates;

    }

    // loop() waits for a command from stdin, parses it and calls the appropriate
    // function. Also intercepts EOF from stdin to ensure gracefully exiting if the
    // GUI dies unexpectedly. When called with some command line arguments, e.g. to
    // run 'bench', once the command is executed the function returns immediately.
    // In addition to the UCI ones, also some additional debug commands are supported.
    void loop (const string &arg)
    {
        RootPos.setup (STARTUP_FEN, Threadpool.main (), Chess960);

        bool running = white_spaces (arg);
        string cmd   = arg;
        string token;
        do
        {
            // Block here waiting for input or EOF
            if (running && !getline (cin, cmd, '\n')) cmd = "quit";

            istringstream iss (cmd);
            token.clear (); // getline() could return empty or blank line
            iss >> skipws >> token;

            if      (white_spaces (token)) continue;
            else if (token == "uci")
            {
                sync_cout
                    << Engine::info ()
                    << Options
                    << "uciok"
                    << sync_endl;
            }
            else if (token == "ucinewgame")
            {
                clear ();
                TimeMgr.available_nodes = 0;
            }
            else if (token == "isready")
            {
                sync_cout << "readyok" << sync_endl;
            }
            else if (token == "setoption")
            {
                iss >> token; // Consume "name" token
                if (token == "name")
                {
                    string name;
                    // Read option-name (can contain spaces) also consume "value" token
                    while (iss >> token && token != "value")
                    {
                        name += string (" ", white_spaces (name) ? 0 : 1) + token;
                    }

                    string value;
                    // Read option-value (can contain spaces)
                    while (iss >> token)
                    {
                        value += string (" ", white_spaces (value) ? 0 : 1) + token;
                    }

                    if (Options.count (name) != 0)
                    {
                        Options[name] = value;
                    }
                    else
                    {
                        sync_cout << "No such option: \'" << name << "\'" << sync_endl;
                    }
                }
            }
            // This sets up the position:
            //  - starting position ("startpos")
            //  - fen-string position ("fen")
            // and then makes the moves given in the following move list ("moves")
            // also saving the moves on stack.
            else if (token == "position")
            {
                string fen;

                iss >> token;  // Consume "startpos" or "fen" token
                if (token == "startpos")
                {
                    fen = STARTUP_FEN;
                    iss >> token;          // Consume "moves" token if any
                }
                else
                if (token == "fen")
                {
                    while (iss >> token && token != "moves") // Consume "moves" token if any
                    {
                        fen += token + " ";
                    }

                    assert (_ok (fen, Chess960, true));
                }
                else
                {
                    goto end_position;
                }

                RootPos.setup (fen, Threadpool.main (), Chess960);

                SetupStates = StateStackPtr (new StateStack);

                if (token == "moves")
                {
                    while (iss >> token)   // Parse and validate game moves (if any)
                    {
                        auto m = move_from_can (token, RootPos);
                        if (MOVE_NONE == m)
                        {
                            std::cerr << "ERROR: Illegal Move '" + token << "'" << std::endl;
                            break;
                        }

                        SetupStates->push (StateInfo ());
                        RootPos.do_move (m, SetupStates->top (), RootPos.gives_check (m, CheckInfo (RootPos)));
                    }
                }
            end_position:;
            }
            // This sets the thinking time and other parameters:
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
            else if (token == "go")
            {
                LimitsT limits;

                limits.start_time = now (); // As early as possible!

                i64 value;
                while (iss >> token)
                {
                    if      (token == "wtime")      { iss >> value; limits.clock[WHITE].time = u32(abs (value)); }
                    else if (token == "btime")      { iss >> value; limits.clock[BLACK].time = u32(abs (value)); }
                    else if (token == "winc")       { iss >> value; limits.clock[WHITE].inc  = u32(abs (value)); }
                    else if (token == "binc")       { iss >> value; limits.clock[BLACK].inc  = u32(abs (value)); }
                    else if (token == "movetime")   { iss >> value; limits.movetime  = u32(abs (value)); }
                    else if (token == "movestogo")  { iss >> value; limits.movestogo = u08(abs (value)); }
                    else if (token == "depth")      { iss >> value; limits.depth     = u08(abs (value)); }
                    else if (token == "nodes")      { iss >> value; limits.nodes     = u64(abs (value)); }
                    else if (token == "mate")       { iss >> value; limits.mate      = u08(abs (value)); }
                    else if (token == "infinite")   { limits.infinite  = true; }
                    else if (token == "ponder")     { limits.ponder    = true; }
                    // parse and validate search moves (if any)
                    else if (token == "searchmoves")
                    {
                        while (iss >> token)
                        {
                            auto m = move_from_can (token, RootPos);
                            if (MOVE_NONE != m)
                            {
                                limits.root_moves.push_back (m);
                            }
                        }
                    }
                }

                Signals.force_stop = true;

                Threadpool.start_main (RootPos, limits, SetupStates);
            }
            // GUI sends 'ponderhit' to tell us to ponder on the same move the
            // opponent has played. In case Signals.ponderhit_stop stream set are
            // waiting for 'ponderhit' to stop the search (for instance because
            // already ran out of time), otherwise should continue searching but
            // switching from pondering to normal search.
            else if ( token == "quit"
                  ||  token == "stop"
                  || (token == "ponderhit" && Signals.ponderhit_stop))
            {
                Signals.force_stop = true;
                Threadpool.main ()->notify_one (); // Could be sleeping
            }
            else if (token == "ponderhit")
            {
                Limits.ponder = false;
            }
            // It is the command to try to register an engine or to tell the engine that registration
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
            else if (token == "register")
            {
                iss >> token;
                if (token == "name")
                {
                    string name;
                    // Read name (can contain spaces)
                    // consume "value" token
                    while (iss >> token && token != "code")
                    {
                        name += string (" ", white_spaces (name) ? 0 : 1) + token;
                    }

                    string code;
                    // Read code (can contain spaces)
                    while (iss >> token)
                    {
                        code += string (" ", white_spaces (code) ? 0 : 1) + token;
                    }
                    //std::cout << name << "\n" << code << std::endl;
                }
                else
                if (token == "later")
                {
                }
            }
            else if (token == "debug")
            {
                iss >> token;
                if (token == "on")  Logger::instance ().start ();
                else
                if (token == "off") Logger::instance ().stop ();
            }
            // Print the root position
            else if (token == "show")
            {
                sync_cout << RootPos << sync_endl;
            }
            // Print the root fen and keys
            else if (token == "keys")
            {
                sync_cout
                    << hex << uppercase << setfill ('0')
                    << "FEN: "                   << RootPos.fen ()      << "\n"
                    << "Posi key: " << setw (16) << RootPos.posi_key () << "\n"
                    << "Matl key: " << setw (16) << RootPos.matl_key () << "\n"
                    << "Pawn key: " << setw (16) << RootPos.pawn_key ()
                    << setfill (' ') << nouppercase << dec
                    << sync_endl;
            }
            else if (token == "moves")
            {
                sync_cout;

                if (RootPos.checkers () != U64 (0))
                {
                    std::cout << "\nEvasion moves: ";
                    for (const auto &m : MoveList<EVASION> (RootPos))
                    {
                        if (RootPos.legal (m))
                        {
                            std::cout << move_to_san (m, RootPos) << " ";
                        }
                    }
                }
                else
                {
                    std::cout << "\nQuiet moves: ";
                    for (const auto &m : MoveList<QUIET> (RootPos))
                    {
                        if (RootPos.legal (m))
                        {
                            std::cout << move_to_san (m, RootPos) << " ";
                        }
                    }

                    std::cout << "\nCheck moves: ";
                    for (const auto &m : MoveList<CHECK> (RootPos))
                    {
                        if (RootPos.legal (m))
                        {
                            std::cout << move_to_san (m, RootPos) << " ";
                        }
                    }

                    std::cout << "\nQuiet Check moves: ";
                    for (const auto &m : MoveList<QUIET_CHECK> (RootPos))
                    {
                        if (RootPos.legal (m))
                        {
                            std::cout << move_to_san (m, RootPos) << " ";
                        }
                    }

                    std::cout << "\nCapture moves: ";
                    for (const auto &m : MoveList<CAPTURE> (RootPos))
                    {
                        if (RootPos.legal (m))
                        {
                            std::cout << move_to_san (m, RootPos) << " ";
                        }
                    }
                }

                std::cout << "\nLegal moves: ";
                for (const auto &m : MoveList<LEGAL> (RootPos))
                {
                    std::cout << move_to_san (m, RootPos) << " ";
                }

                std::cout << sync_endl;
            }
            else if (token == "flip")
            {
                RootPos.flip ();
            }
            else if (token == "eval")
            {
                sync_cout << Evaluator::trace (RootPos) << sync_endl;
            }
            else if (token == "perft")
            {
                i32    depth;
                string fen_fn;
                depth  = ((iss >> depth) ? depth : 1);
                fen_fn = ((iss >> fen_fn) ? fen_fn : "");
                
                stringstream ss;
                ss  << i32(Options["Hash"])    << " "
                    << i32(Options["Threads"]) << " "
                    << depth << " perft " << fen_fn;

                benchmark (ss, RootPos);
            }
            else if (token == "bench")      benchmark (iss, RootPos);
            //else if (token == "autotune")   auto_tune (iss);
            //else if (!white_spaces (token)) system (token.c_str ());
            else
            {
                sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl;
            }

        } while (running && cmd != "quit");

        Threadpool.main ()->join (); // Cannot quit whilst the search is running
    }

}
