#include "UCI.h"

#include <iostream>
#include <cstdarg>

#include "xcstring.h"
#include "xstring.h"
#include "atomicstream.h"

#include "Engine.h"
#include "Transposition.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Benchmark.h"
#include "Notation.h"
#include "TriLogger.h"

//#include "Thread.h"

namespace UCI {

    using namespace Searcher;

    typedef std::istringstream cmdstream;

    using std::string;
    using std::atom;
    using std::endl;

    namespace {

        // Root position
        Position            rootPos = Position (int8_t (0));

        // Keep track of position keys along the setup moves
        // (from start position to the position just before to start searching).
        // Needed by repetition draw detection.
        StateInfoStackPtr   setupStates;

        bool active = false;

#pragma region uci-commands

        void exe_uci ()
        {
            atom ()
                << Engine::info (true) << '\n' 
                << (Options) << '\n'
                << "uciok" << endl;
        }

        void exe_ucinewgame ()
        {
            TT.clear ();
        }

        void exe_isready ()
        {
            atom () << "readyok" << endl;
        }

        void exe_setoption (cmdstream &cstm)
        {
            string token;
            if (!(cstm >> token)) return; // consume "name" token
            if (iequals (token, "name"))
            {
                string name;
                // Read option-name (can contain spaces)
                while (cstm.good ())
                {
                    if (!(cstm >> token)) return;
                    if (iequals (token, "value")) break;
                    name += whitespace (name) ? token : " " + token;
                }

                string value;
                // Read option-value (can contain spaces)
                while (cstm.good ())
                {
                    if (!(cstm >> token)) return;
                    value += whitespace (value) ? token : " " + token;
                }

                if (Options.count (name) > 0)
                {
                    (*Options[name]) = value;
                    //atom () << (*Options[name])();
                }
                else
                {
                    //atom () << "WHAT??? No such option: \'" << name << "\'";
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
            // consume "startpos" or "fen" token
            if (!(cstm >> token)) return;

            string fen;
            // consume "moves" token if any
            if (iequals (token, "startpos"))
            {
                fen = FEN_N;
                if (!(cstm >> token)) return;
            }
            else if (iequals (token, "fen"))
            {
                while (cstm.good () && (cstm >> token))
                {
                    if (iequals (token, "moves")) break;
                    fen += whitespace (fen) ? token : " " + token;
                }

                bool ok_fen = _ok (fen);
                ASSERT (ok_fen);
                if (!ok_fen) return;
            }
            else return;

            rootPos.setup (fen, *(Options["UCI_Chess960"]));

            if (iequals (token, "moves"))
            {
                setupStates = StateInfoStackPtr (new StateInfoStack ());

                // parse move list (if any)
                while (cstm.good () && (cstm >> token))
                {
                    Move m = move_from_can (token, rootPos);
                    if (MOVE_NONE == m)
                    {
                        TRI_LOG_MSG ("ERROR: Illegal Move" << token);
                        break;
                    }
                    setupStates->push (StateInfo ());
                    rootPos.do_move (m, setupStates->top ());
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
            Limits limits;
            string token;

            while (cstm.good () && (cstm >> token))
            {
                if (false);
                else if (iequals (token, "searchmoves"))
                {
                    while (cstm.good () && (cstm >> token))
                    {
                        Move m = move_from_can (token, rootPos);
                        if (MOVE_NONE == m) continue;
                        limits.search_moves.emplace_back (m);
                    }
                }
                else if (iequals (token, "wtime"))      cstm >> limits.game_clock[WHITE].time;
                else if (iequals (token, "btime"))      cstm >> limits.game_clock[BLACK].time;
                else if (iequals (token, "winc"))       cstm >> limits.game_clock[WHITE].inc;
                else if (iequals (token, "binc"))       cstm >> limits.game_clock[BLACK].inc;
                else if (iequals (token, "movetime"))   cstm >> limits.move_time;
                else if (iequals (token, "movestogo"))  cstm >> limits.moves_to_go;
                else if (iequals (token, "depth"))      cstm >> limits.depth;
                else if (iequals (token, "nodes"))      cstm >> limits.nodes;
                else if (iequals (token, "mate"))       cstm >> limits.mate_in;
                else if (iequals (token, "infinite"))   limits.infinite  = true;
                else if (iequals (token, "ponder"))     limits.ponder    = true;
            }

            //Threads.start_thinking (rootPos, limits, setupStates);
        }

        void exe_ponderhit ()
        {
            limits.ponder = false;
        }

        void exe_debug (cmdstream &cstm)
        {
            // debug on/off
        }

        void exe_print ()
        {
            atom () << rootPos << endl;
        }

        void exe_key ()
        {
            atom () << std::hex << std::uppercase << std::setfill('0')
                << "fen: " << rootPos.fen () << '\n'
                << "posi key: " << std::setw (16) << rootPos.posi_key () << '\n'
                << "matl key: " << std::setw (16) << rootPos.matl_key () << '\n'
                << "pawn key: " << std::setw (16) << rootPos.pawn_key ()
                << std::dec << endl;
        }

        void exe_flip ()
        {
            rootPos.flip ();
        }

        void exe_eval ()
        {
            atom () << Evaluator::trace (rootPos) << endl;
        }

        void exe_perft (cmdstream &cstm)
        {
            string token;
            // Read perft depth
            if (cstm.good () && (cstm >> token))
            {
                std::stringstream ss;
                ss << Options["Hash"] << " " << Options["Threads"] << " " << token << " current perft";
                benchmark (ss, rootPos);
            }
        }

        void exe_stop ()
        {
            signals.stop = true;
            // Could be sleeping
            //Threads.main ()->notify_one();
        }

        void exe_quit ()
        {
            //Search::stop ();
            //Trans::destroy ();
            //Thread::destroy ();
            //Book::close ();
            //GTB::close ();
        }

#pragma endregion

    }

    void start (const string &args)
    {
        init_options ();

        rootPos.setup (FEN_N, *(Options["UCI_Chess960"]));

        active = args.empty ();
        string cmd = args;
        string token;
        do
        {
            // Block here waiting for input
            if (active && !std::getline (std::cin, cmd, '\n')) cmd = "quit";
            if (std::whitespace (cmd)) continue;
            
            try
            {
                cmdstream cstm (cmd);
                cstm >> std::skipws >> token;

                if (false);
                else if (iequals (token, "uci"))        exe_uci ();
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
                    signals.stop_on_ponderhit ? exe_stop () : exe_ponderhit ();
                }
                else if (iequals (token, "debug"))      exe_debug (cstm);
                else if (iequals (token, "print"))      exe_print ();
                else if (iequals (token, "key"))        exe_key ();
                else if (iequals (token, "flip"))       exe_flip ();
                else if (iequals (token, "eval"))       exe_eval ();
                else if (iequals (token, "perft"))      exe_perft (cstm);
                else if (iequals (token, "bench"))      benchmark (cstm, rootPos);
                else if (iequals (token, "quit")
                    || iequals (token, "stop"))         exe_stop ();
                else
                {
                    TRI_LOG_MSG ("WHAT??? No such command: \'" << cmd << "\'");
                }
            
            }
            catch (std::exception &exp)
            {
                TRI_LOG_MSG (exp.what ());
            }
        }
        while (active && !iequals (cmd, "quit"));

        // Cannot quit while search stream active
        //Threads.wait_for_think_finished (); 
    }

    void stop ()
    {
        active = false;
    }

    void send_responce (const char format[], ...)
    {
        try
        {
            static char buf[1024];
            size_t size  =   sizeof (buf);
            size_t count = _countof (buf);
            std::memset (buf, 0, size);
            va_list args;
            va_start (args, format);
            int32_t copied = vsnprintf_s (buf, count, _TRUNCATE, format, args);
            va_end (args);
            if (copied != -1)
            {
                buf[copied] = '\0';
                atom () << buf << endl;
            }
        }
        catch (...)
        {
        }
    }

}
