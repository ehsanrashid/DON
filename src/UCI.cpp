#include "UCI.h"

#include <iostream>
#include <cstdarg>
#include "xcstring.h"
#include "xstring.h"
#include "atomicstream.h"

#include "Engine.h"
#include "UCI.Option.h"
#include "Transposition.h"
#include "Searcher.h"
#include "Evaluator.h"
#include "Benchmark.h"
#include "Notation.h"
#include "trilogger.h"

//#include "Thread.h"

namespace UCI {

    namespace {

        // Root position
        Position            pos;

        // Keep track of position keys along the setup moves
        // (from start position to the position just before to start searching).
        // Needed by repetition draw detection.
        StateInfoStackPtr   setup_states;


        bool is_running = false;

#pragma region uci-commands

        void exe_uci ()
        {
            std::atom ()
                << Engine::info (true) << std::endl 
                << (Options) << std::endl
                << "uciok" << std::endl;
        }

        void exe_ucinewgame ()
        {
            TT.clear ();
        }

        void exe_isready ()
        {
            std::atom () << "readyok" << std::endl;
        }

        void exe_setoption (::std::istringstream &cstm)
        {
            ::std::string token;
            if (!(cstm >> token)) return; // consume "name" token
            if (iequals (token, "name"))
            {
                ::std::string name;
                // Read option-name (can contain spaces)
                while (cstm.good ())
                {
                    if (!(cstm >> token)) return;
                    if (iequals (token, "value")) break;
                    name += iswhitespace (name) ? token : " " + token;
                }

                ::std::string value;
                // Read option-value (can contain spaces)
                while (cstm.good ())
                {
                    if (!(cstm >> token)) return;
                    value += iswhitespace (value) ? token : " " + token;
                }

                if (Options.count (name) > 0)
                {
                    (*Options[name]) = value;
                    //std::atom () << (*Options[name])();
                }
                else
                {
                    //std::atom () << "WHAT??? No such option: '" << name << "'";
                }
            }
        }

        void exe_register (::std::istringstream &cstm)
        {
            //this is the command to try to register an engine or to tell the engine that registration
            //will be done later. This command should always be sent if the engine has sent "registration error"
            //at program startup.
            //The following tokens are allowed:
            //* later
            //   the user doesn't want to register the engine now.
            //* name <x>
            //   the engine should be registered with the name <x>
            //* code <y>
            //   the engine should be registered with the code <y>
            //Example:
            //   "register later"
            //   "register name Stefan MK code 4359874324"

        }

        // position(cmd, pos) is called when engine receives the "position" UCI command.
        // The function sets up the position:
        //  - starting position ("startpos")
        //  - fen-string position ("fen")
        // and then makes the moves given in the following move list ("moves")
        // also saving the moves on stack.
        void exe_position (::std::istringstream &cstm)
        {
            ::std::string token;
            // consume "startpos" or "fen" token
            if (!(cstm >> token)) return;

            ::std::string fen;
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
                    fen += iswhitespace (fen) ? token : " " + token;
                }

                bool ok_fen = _ok (fen);
                ASSERT (ok_fen);
                if (!ok_fen) return;
            }
            else return;

            pos.setup (fen, *(Options["UCI_Chess960"]));

            if (iequals (token, "moves"))
            {
                setup_states = StateInfoStackPtr (new std::stack<StateInfo> ());

                // parse move list (if any)
                while (cstm.good () && (cstm >> token))
                {
                    Move m = move_from_can (token, pos);
                    if (MOVE_NONE == m)
                    {
                        TRI_LOG_MSG ("ERROR: Illegal Move" << token);
                        break;
                    }
                    setup_states->push (StateInfo ());
                    pos.do_move (m, setup_states->top ());
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
        void exe_go (::std::istringstream &cstm)
        {
            Searcher::Limits limits;
            ::std::string token;

            while (cstm.good () && (cstm >> token))
            {
                try
                {
                    if (false);
                    else if (iequals (token, "searchmoves"))
                    {
                        //limits.search_moves.clear ();
                        while (cstm.good () && (cstm >> token))
                        {
                            Move m = move_from_can (token, pos);
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
                catch (const std::exception &)
                {}
            }

            //Threads.start_thinking(pos, setup_states, limits);

        }

        void exe_ponderhit ()
        {
            //Search::Signals.stop = true;
            //Threads.main()->notify_one(); // Could be sleeping
        }

        void exe_debug (::std::istringstream &cstm)
        {
            // debug on/off
        }

        void exe_print ()
        {
            std::atom () << pos << std::endl;
        }

        void exe_flip ()
        {
            pos.flip ();
        }

        void exe_eval ()
        {
            //std::atom () << Eval::trace (pos) << std::endl;
        }

        void exe_perft (::std::istringstream &cstm)
        {
            ::std::string token;
            // Read perft depth
            if (cstm.good () && (cstm >> token))
            {
                ::std::stringstream ss;
                //ss << Options["Hash"] << " " << Options["Threads"] << " " << token << " current perft";
                benchmark (ss, pos);
            }
        }

        void exe_stop ()
        {
            //Search::Signals.stop = true;
            //Threads.main_thread()->notify_one(); // Could be sleeping
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

    void start (const ::std::string &args)
    {
        init_options ();

        pos.setup (FEN_N, *(Options["UCI_Chess960"]));

        is_running = args.empty ();
        ::std::string cmd = args;
        ::std::string token;
        do
        {
            // Block here waiting for input
            if (is_running && !std::getline (std::cin, cmd, '\n')) cmd = "quit";
            if (std::iswhitespace (cmd)) continue;
            try
            {
                ::std::istringstream cstm (cmd);
                cstm >> std::skipws >> token;

                if (false);
                else if (iequals (token, "uci"))        exe_uci ();
                else if (iequals (token, "ucinewgame")) exe_ucinewgame ();
                else if (iequals (token, "isready"))    exe_isready ();
                else if (iequals (token, "setoption"))  exe_setoption (cstm);
                else if (iequals (token, "register"))   exe_register (cstm);
                else if (iequals (token, "position"))   exe_position (cstm);
                else if (iequals (token, "go"))         exe_go (cstm);
                else if (iequals (token, "debug"))      exe_debug (cstm);
                else if (iequals (token, "ponderhit"))
                {
                    // GUI sends 'ponderhit' to tell us to ponder on the same move the
                    // opponent has played. In case Signals.stopOnPonderhit stream set we are
                    // waiting for 'ponderhit' to stop the search (for instance because we
                    // already ran out of time), otherwise we should continue searching but
                    // switching from pondering to normal search.
                    false/*Search::Signals.stopOnPonderhit*/ ? exe_stop () : exe_ponderhit ();
                }
                else if (iequals (token, "print"))      exe_print ();
                else if (iequals (token, "flip"))       exe_flip ();
                else if (iequals (token, "eval"))       exe_eval ();
                else if (iequals (token, "perft"))      exe_perft (cstm);
                else if (iequals (token, "bench"))      benchmark (cstm, pos);
                else if (iequals (token, "quit")
                    || iequals (token, "stop"))         exe_stop ();

                else
                {
                    //std::atom () << "WHAT??? No such command: '" << cmd << "'";
                }
            }
            catch (...)
            {}
        }
        while (is_running && !iequals (cmd, "quit"));

        //Threads.wait_for_think_finished(); // Cannot quit while search stream is_running
    }

    void stop ()
    {
        is_running = false;
    }


    void send_responce (const char format[], ...)
    {
        try
        {
            static char buf[1024];
            size_t size  =   sizeof (buf);
            size_t count = _countof (buf);
            ::std::memset (buf, 0, size);
            va_list args;
            va_start (args, format);
            int32_t copied = vsnprintf_s (buf, count, _TRUNCATE, format, args);
            va_end (args);
            if (copied != -1)
            {
                buf[copied] = '\0';
                std::atom () << buf << std::endl;
            }
        }
        catch (...)
        {
        }
    }

}