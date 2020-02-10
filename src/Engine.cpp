#include "Engine.h"

#include <algorithm>
#include <array>
#include <cstdarg>
#include <iostream>
#include <fstream>
#include <memory>

#include "BitBoard.h"
#include "BitBases.h"
#include "Endgame.h"
#include "Evaluator.h"
#include "Logger.h"
#include "Material.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Option.h"
#include "Pawns.h"
#include "Polyglot.h"
#include "PSQTable.h"
#include "Searcher.h"
#include "TBsyzygy.h"
#include "Thread.h"
#include "Transposition.h"
#include "Zobrist.h"

using namespace std;

namespace {

    using namespace Searcher;
    using namespace TBSyzygy;

    // Engine Name
    const string Name{ "DON" };
    // Version number. If version is left empty, then show compile date in the format YY-MM-DD.
    const string Version{ "" };
    // Author Name
    const string Author{ "Ehsan Rashid" };

    /// Forsyth-Edwards Notation (FEN) is a standard notation for describing a particular board position of a chess game.
    /// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.
    const string StartFEN{ "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" };

    const vector<string> DefaultCmds
    {
        // ---Chess Normal---
        "setoption name UCI_Chess960 value false",
        "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
        "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 10",
        "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 11",
        "4rrk1/pp1n3p/3q2pQ/2p1pb2/2PP4/2P3N1/P2B2PP/4RRK1 b - - 7 19",
        "rq3rk1/ppp2ppp/1bnpb3/3N2B1/3NP3/7P/PPPQ1PP1/2KR3R w - - 7 14 moves d4e6",
        "r1bq1r1k/1pp1n1pp/1p1p4/4p2Q/4Pp2/1BNP4/PPP2PPP/3R1RK1 w - - 2 14 moves g2g4",
        "r3r1k1/2p2ppp/p1p1bn2/8/1q2P3/2NPQN2/PPP3PP/R4RK1 b - - 2 15",
        "r1bbk1nr/pp3p1p/2n5/1N4p1/2Np1B2/8/PPP2PPP/2KR1B1R w kq - 0 13",
        "r1bq1rk1/ppp1nppp/4n3/3p3Q/3P4/1BP1B3/PP1N2PP/R4RK1 w - - 1 16",
        "4r1k1/r1q2ppp/ppp2n2/4P3/5Rb1/1N1BQ3/PPP3PP/R5K1 w - - 1 17",
        "2rqkb1r/ppp2p2/2npb1p1/1N1Nn2p/2P1PP2/8/PP2B1PP/R1BQK2R b KQ - 0 11",
        "r1bq1r1k/b1p1npp1/p2p3p/1p6/3PP3/1B2NN2/PP3PPP/R2Q1RK1 w - - 1 16",
        "3r1rk1/p5pp/bpp1pp2/8/q1PP1P2/b3P3/P2NQRPP/1R2B1K1 b - - 6 22",
        "r1q2rk1/2p1bppp/2Pp4/p6b/Q1PNp3/4B3/PP1R1PPP/2K4R w - - 2 18",
        "4k2r/1pb2ppp/1p2p3/1R1p4/3P4/2r1PN2/P4PPP/1R4K1 b - - 3 22",
        "3q2k1/pb3p1p/4pbp1/2r5/PpN2N2/1P2P2P/5PP1/Q2R2K1 b - - 4 26",
        "6k1/6p1/6Pp/ppp5/3pn2P/1P3K2/1PP2P2/3N4 b - - 0 1",
        "3b4/5kp1/1p1p1p1p/pP1PpP1P/P1P1P3/3KN3/8/8 w - - 0 1",
        "2K5/p7/7P/5pR1/8/5k2/r7/8 w - - 0 1 moves g5g6 f3e3 g6g5 e3f3",
        "8/6pk/1p6/8/PP3p1p/5P2/4KP1q/3Q4 w - - 0 1",
        "7k/3p2pp/4q3/8/4Q3/5Kp1/P6b/8 w - - 0 1",
        "8/2p5/8/2kPKp1p/2p4P/2P5/3P4/8 w - - 0 1",
        "8/1p3pp1/7p/5P1P/2k3P1/8/2K2P2/8 w - - 0 1",
        "8/pp2r1k1/2p1p3/3pP2p/1P1P1P1P/P5KR/8/8 w - - 0 1",
        "8/3p4/p1bk3p/Pp6/1Kp1PpPp/2P2P1P/2P5/5B2 b - - 0 1",
        "5k2/7R/4P2p/5K2/p1r2P1p/8/8/8 b - - 0 1",
        "6k1/6p1/P6p/r1N5/5p2/7P/1b3PP1/4R1K1 w - - 0 1",
        "1r3k2/4q3/2Pp3b/3Bp3/2Q2p2/1p1P2P1/1P2KP2/3N4 w - - 0 1",
        "6k1/4pp1p/3p2p1/P1pPb3/R7/1r2P1PP/3B1P2/6K1 w - - 0 1",
        "8/3p3B/5p2/5P2/p7/PP5b/k7/6K1 w - - 0 1",
        "5rk1/q6p/2p3bR/1pPp1rP1/1P1Pp3/P3B1Q1/1K3P2/R7 w - - 93 90",
        "4rrk1/1p1nq3/p7/2p1P1pp/3P2bp/3Q1Bn1/PPPB4/1K2R1NR w - - 40 21",
        "r3k2r/3nnpbp/q2pp1p1/p7/Pp1PPPP1/4BNN1/1P5P/R2Q1RK1 w kq - 0 16",
        "3Qb1k1/1r2ppb1/pN1n2q1/Pp1Pp1Pr/4P2p/4BP2/4B1R1/1R5K b - - 11 40",
        // 5-men positions
        "8/8/8/8/5kp1/P7/8/1K1N4 w - - 0 80",     // Kc2 - Mate
        "8/8/8/5N2/8/p7/8/2NK3k w - - 0 82",      // Na2 - Mate
        "8/3k4/8/8/8/4B3/4KB2/2B5 w - - 0 85",    // Draw
        // 6-men positions
        "8/8/1P6/5pr1/8/4R3/7k/2K5 w - - 0 92",   // Re5 - Mate
        "8/2p4P/8/kr6/6R1/8/8/1K6 w - - 0 94",    // Ka2 - Mate
        "8/8/3P3k/8/1p6/8/1P6/1K3n2 b - - 0 90",  // Nd2 - Draw
        // 7-men positions
        "8/R7/2q5/8/6k1/8/1P5p/K6R w - - 0 124", // Draw
        // Mate and stalemate positions
        "6k1/3b3r/1p1p4/p1n2p2/1PPNpP1q/P3Q1p1/1R1RB1P1/5K2 b - - 0 1",
        "r2r1n2/pp2bk2/2p1p2p/3q4/3PN1QP/2P3R1/P4PP1/5RK1 w - - 0 1",
        "8/8/8/8/8/6k1/6p1/6K1 w - - 0 1",
        "7k/7P/6K1/8/3B4/8/8/8 b - - 0 1",

        // ---Chess 960---
        "setoption name UCI_Chess960 value true",
        "bbqnnrkr/pppppppp/8/8/8/8/PPPPPPPP/BBQNNRKR w HFhf - 0 1 moves g2g3 d7d5 d2d4 c8h3 c1g5 e8d6 g5e7 f7f6",
    };

    const array<string, 12> Months { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    i32 month(const string &mmm)
    {
        // for (u32 m = 0; m < Months.size(); ++m)
        // {
        //     if (mmm == Months[m])
        //     {
        //         return m+1;
        //     }
        // }
        // return 0;
        auto itr = std::find(Months.begin(), Months.end(), mmm);
        return itr != Months.end() ? std::distance(Months.begin(), itr) + 1 : 0;
    }

    /// engineInfo() returns a string trying to describe the engine
    const string engineInfo()
    {
        ostringstream oss;

        oss << setfill('0');
#    if defined(VER)
        oss << VER;
#    else
        if (whiteSpaces(Version))
        {
            // From compiler, format is "Sep 2 1982"
            istringstream iss{ __DATE__ };
            string mmm, dd, yyyy;
            iss >> mmm >> dd >> yyyy;
            oss << setw(2) << yyyy.substr(2)
                << setw(2) << month(mmm)
                << setw(2) << dd;
        }
        else
        {
            oss << Version;
        }
#    endif
        oss << setfill(' ');

        oss <<
#       if defined(BIT64)
            ".64"
#       else
            ".32"
#       endif
        ;

        oss <<
#       if defined(BM2)
            ".BM2"
#       elif defined(ABM)
            ".ABM"
#       else
            ""
#       endif
        ;

        return oss.str();
    }

    /// compilerInfo() returns a string trying to describe the compiler used
    const string compilerInfo()
    {
        #define STRINGIFY(x)                    #x
        #define STRING(x)                       STRINGIFY(x)
        #define VER_STRING(major, minor, patch) STRING(major) "." STRING(minor) "." STRING(patch)

        ostringstream oss;
        oss << "\nCompiled by ";

        #ifdef __clang__
            oss << "clang++ ";
            oss << VER_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
        #elif __INTEL_COMPILER
            oss << "Intel compiler ";
            oss << "(version " STRING(__INTEL_COMPILER) " update " STRING(__INTEL_COMPILER_UPDATE) ")";
        #elif _MSC_VER
            oss << "MSVC ";
            oss << "(version " STRING(_MSC_FULL_VER) "." STRING(_MSC_BUILD) ")";
        #elif __GNUC__
            oss << "g++ (GNUC) ";
            oss << VER_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
        #else
            oss << "Unknown compiler ";
            oss << "(unknown version)";
        #endif

        #if defined(__APPLE__)
            oss << " on Apple";
        #elif defined(__CYGWIN__)
            oss << " on Cygwin";
        #elif defined(__MINGW64__)
            oss << " on MinGW64";
        #elif defined(__MINGW32__)
            oss << " on MinGW32";
        #elif defined(__ANDROID__)
            oss << " on Android";
        #elif defined(__linux__)
            oss << " on Linux";
        #elif defined(_WIN32)
            oss << " on Microsoft Windows 32-bit";
        #elif defined(_WIN64)
            oss << " on Microsoft Windows 64-bit";
        #else
            oss << " on unknown system";
        #endif

        oss << "\n __VERSION__ macro expands to: ";
        #ifdef __VERSION__
            oss << __VERSION__;
        #else
            oss << "(undefined macro)";
        #endif
        oss << "\n";

        return oss.str();
    }

    /// setoption() function updates the UCI option ("name") to the given value ("value").
    void setOption(istringstream &iss)
    {
        string token;
        iss >> token; // Consume "name" token

        //if (token != "name") return;
        string name;
        // Read option-name (can contain spaces)
        while (   (iss >> token)
               && token != "value") // Consume "value" token if any
        {
            name += (name.empty() ? "" : " ") + token;
        }

        //if (token != "value") return;
        string value;
        // Read option-value (can contain spaces)
        while (iss >> token)
        {
            value += (value.empty() ? "" : " ") + token;
        }

        if (Options.find(name) != Options.end())
        {
            Options[name] = value;
        }
        else
        {
            sync_cout << "No such option: \'" << name << "\'" << sync_endl;
        }
    }

    /// position() sets up the starting position "startpos"/"fen <fenstring>" and then
    /// makes the moves given in the move list "moves" also saving the moves on stack.
    void position(istringstream &iss, Position &pos, StateListPtr &states)
    {
        string token;
        iss >> token; // Consume "startpos" or "fen" token

        string fen;
        if (token == "startpos")
        {
            fen = StartFEN;
            iss >> token; // Consume "moves" token if any
        }
        else
        //if (token == "fen")
        {
            while (iss >> token) // Consume "moves" token if any
            {
                if (token == "moves")
                {
                    break;
                }
                fen += token + " ";
                token.clear();
            }
            //assert(isOk(fen));
        }
        assert(token == "" || token == "moves");
        // Drop old and create a new one
        states = StateListPtr{new std::deque<StateInfo>(1)};
                //std::make_unique<std::deque<StateInfo>>(new std::deque<StateInfo>(1));
        pos.setup(fen, states->back(), pos.thread);
        //assert(pos.fen() == fullTrim(fen));

        u16 count = 0;
        // Parse and validate moves (if any)
        while (iss >> token)
        {
            ++count;
            auto m = canMove(token, pos);
            if (MOVE_NONE == m)
            {
                cerr << "ERROR: Illegal Move '" << token << "' at " << count << endl;
                break;
            }
            states->emplace_back();
            pos.doMove(m, states->back());
        }
    }

    /// go() sets the thinking time and other parameters from the input string, then starts the search.
    void go(istringstream &iss, Position &pos, StateListPtr &states)
    {
        Threadpool.stop = true;
        Threadpool.mainThread()->waitIdle();

        Limit limit;
        vector<Move> searchMoves; // Restrict search to these root moves only
        bool ponder = false;

        string token;
        while (iss >> token)
        {
            if (token == "wtime")     iss >> limit.clock[WHITE].time;
            else
            if (token == "btime")     iss >> limit.clock[BLACK].time;
            else
            if (token == "winc")      iss >> limit.clock[WHITE].inc;
            else
            if (token == "binc")      iss >> limit.clock[BLACK].inc;
            else
            if (token == "movestogo") iss >> limit.movestogo;
            else
            if (token == "movetime")  iss >> limit.moveTime;
            else
            if (token == "depth")     iss >> limit.depth;
            else
            if (token == "nodes")     iss >> limit.nodes;
            else
            if (token == "mate")      iss >> limit.mate;
            else
            if (token == "infinite")  limit.infinite = true;
            else
            if (token == "ponder")    ponder = true;
            else
            // Parse and Validate search-moves (if any)
            if (token == "searchmoves")
            {
                while (iss >> token)
                {
                    auto m = canMove(token, pos);
                    if (MOVE_NONE == m)
                    {
                        cerr << "ERROR: Illegal Rootmove '" << token << "'" << endl;
                        continue;
                    }
                    searchMoves.push_back(m);
                }
            }
            else
            if (token == "ignoremoves")
            {
                for (const auto &vm : MoveList<GenType::LEGAL>(pos))
                {
                    searchMoves.push_back(vm);
                }
                while (iss >> token)
                {
                    auto m = canMove(token, pos);
                    if (MOVE_NONE == m)
                    {
                        cerr << "ERROR: Illegal Rootmove '" << token << "'" << endl;
                        continue;
                    }
                    searchMoves.erase(std::remove(searchMoves.begin(), searchMoves.end(), m), searchMoves.end());
                }
            }
            else
            if (token == "perft")
            {
                Depth depth = 1;
                iss >> depth;
                bool detail = false;
                iss >> boolalpha >> detail;
                perft<true>(pos, depth, detail);
                return;
            }
            else
            {
                cerr << "ERROR: Illegal token : " << token << endl;
                return;
            }
        }
        Threadpool.startThinking(pos, states, limit, searchMoves, ponder);
    }

    /// setupBench() builds a list of UCI commands to be run by bench.
    /// There are five parameters:
    /// - TT size in MB (default is 16)
    /// - Threads count(default is 1)
    /// - limit value (default is 13)
    /// - limit type:
    ///     * depth (default)
    ///     * movetime
    ///     * nodes
    ///     * mate
    ///     * perft
    /// - FEN positions to be used in FEN format
    ///     * 'default' for builtin positions (default)
    ///     * 'current' for current position
    ///     * '<filename>' for file containing FEN positions
    /// example:
    /// bench -> search default positions up to depth 13
    /// bench 64 1 15 -> search default positions up to depth 15 (TT = 64MB)
    /// bench 64 4 5000 movetime current -> search current position with 4 threads for 5 sec (TT = 64MB)
    /// bench 64 1 100000 nodes -> search default positions for 100K nodes (TT = 64MB)
    /// bench 16 1 5 perft -> run perft 5 on default positions
    vector<string> setupBench(istringstream &iss, const Position &pos)
    {
        string token;

        // Assign default values to missing arguments
        string    hash = (iss >> token) && !whiteSpaces(token) ? token : "16";
        string threads = (iss >> token) && !whiteSpaces(token) ? token : "1";
        string   value = (iss >> token) && !whiteSpaces(token) ? token : "13";
        string    mode = (iss >> token) && !whiteSpaces(token) ? token : "depth";
        string     fen = (iss >> token) && !whiteSpaces(token) ? token : "default";

        vector<string> cmds;
        vector<string> uciCmds;

        if (fen == "current")
        {
            cmds.push_back(pos.fen());
        }
        else
        if (fen == "default")
        {
            cmds = DefaultCmds;
        }
        else
        {
            ifstream ifs(fen, ios_base::in);
            if (!ifs.is_open())
            {
                cerr << "ERROR: unable to open file ... \'" << fen << "\'" << endl;
                return uciCmds;
            }
            string cmd;
            while (std::getline(ifs, cmd, '\n'))
            {
                if (!cmd.empty())
                {
                    cmds.push_back(cmd);
                }
            }
            ifs.close();
        }

        bool chess960 = bool(Options["UCI_Chess960"]);

        uciCmds.push_back("setoption name Threads value " + threads);
        uciCmds.push_back("setoption name Hash value " + hash);
        uciCmds.push_back("ucinewgame");

        string go = mode == "eval" ? "eval" : "go " + mode + " " + value;

        for (const auto &cmd : cmds)
        {
            if (cmd.find("setoption") != string::npos)
            {
                uciCmds.push_back(cmd);
            }
            else
            {
                uciCmds.push_back("position fen " + cmd);
                uciCmds.push_back(go);
            }
        }

        if (fen != "current")
        {
            uciCmds.push_back("setoption name UCI_Chess960 value " + string(chess960 ? "true" : "false"));
            uciCmds.push_back("position fen " + pos.fen());
        }
        return uciCmds;
    }

    /// bench() setup list of UCI commands is setup according to bench parameters,
    /// then it is run one by one printing a summary at the end.
    void bench(istringstream &iss, Position &pos, StateListPtr &states)
    {
        const auto uciCmds = setupBench(iss, pos);
        const auto count = u16(std::count_if(uciCmds.begin(), uciCmds.end(),
                                            [](const string &s) { return 0 == s.find("go ")
                                                                      || 0 == s.find("eval"); }));
        u16 i = 0;

        initializeDebug();

        auto elapsedTime = now();
        u64 nodes = 0;
        for (const auto &cmd : uciCmds)
        {
            istringstream is{cmd};
            string token;
            token.clear();
            is >> skipws >> token;

            if (token.empty())
            {
                continue;
            }

            if (token == "position")
            {
                position(is, pos, states);
            }
            else
            if (token == "go"
             || token == "eval")
            {
                cerr << "\n---------------\n"
                     << "Position: " << right
                     << setw(2) << ++i << '/' << count << " "
                     << left << pos.fen() << endl;

                if (token == "go")
                {
                    go(is, pos, states);
                    Threadpool.mainThread()->waitIdle();
                    nodes += Threadpool.sum(&Thread::nodes);
                }
                else
                {
                    sync_cout << trace(pos) << sync_endl;
                }
            }
            else
            if (token == "setoption")
            {
                setOption(is);
            }
            else
            if (token == "ucinewgame")
            {
                clear();
                elapsedTime = now();
            }
            else
            {
                cerr << "Unknown command: \'" << token << "\'" << endl;
            }
        }

        elapsedTime = std::max(now() - elapsedTime, TimePoint(1));

        debugPrint(); // Just before exiting

        cerr << right
             << "\n=================================\n"
             << "Total time (ms) :" << setw(16) << elapsedTime << "\n"
             << "Nodes searched  :" << setw(16) << nodes        << "\n"
             << "Nodes/second    :" << setw(16) << nodes * 1000 / elapsedTime
             << "\n---------------------------------\n"
             << left << endl;
    }

    /// processInput() Waits for a command from stdin, parses it and calls the appropriate function.
    /// Also intercepts EOF from stdin to ensure gracefully exiting if the GUI dies unexpectedly.
    /// Single command line arguments is executed once and returns immediately, e.g. 'bench'.
    /// In addition to the UCI ones, also some additional commands are supported.
    void processInput(u32 argc, const char *const *argv)
    {
        // Join arguments
        string cmd;
        for (u32 i = 1; i < argc; ++i)
        {
            cmd += string(argv[i]) + " ";
        }

        initializeDebug();

        Position pos;
        // Stack to keep track of the position states along the setup moves
        // (from the start position to the position just before the search starts).
        // Needed by 'draw by repetition' detection.
        StateListPtr states{new std::deque<StateInfo>(1)};
        //auto states = std::make_unique<std::deque<StateInfo>>(new std::deque<StateInfo>(1));

        auto ui_thread = std::make_shared<Thread>(0);
        pos.setup(StartFEN, states->back(), ui_thread.get());

        do
        {
            if (   1 == argc
                && !std::getline(cin, cmd, '\n')) // Block here waiting for input or EOF
            {
                cmd = "quit";
            }

            istringstream iss{ cmd };
            string token;
            token.clear(); // Avoid a stale if getline() returns empty or blank line
            iss >> skipws >> token;

            if (token.empty())
            {
                continue;
            }

            if (token == "quit"
             || token == "stop")
            {
                Threadpool.stop = true;
            }
            else
            // GUI sends 'ponderhit' to tell that the opponent has played the expected move.
            // So 'ponderhit' will be sent if told to ponder on the same move the opponent has played.
            // Now should continue searching but switch from pondering to normal search.
            if (token == "ponderhit")
            {
                Threadpool.mainThread()->ponder = false; // Switch to normal search
            }
            else
            if (token == "isready")
            {
                sync_cout << "readyok" << sync_endl;
            }
            else
            if (token == "uci")
            {
                sync_cout << "id name " << Name << " " << engineInfo() << "\n"
                          << "id author " << Author << "\n"
                          << Options
                          << "uciok" << sync_endl;
            }
            else
            if (token == "ucinewgame")
            {
                clear();
            }
            else
            if (token == "position")
            {
                position(iss, pos, states);
            }
            else
            if (token == "go")
            {
                go(iss, pos, states);
            }
            else
            if (token == "setoption")
            {
                setOption(iss);
            }
            // Additional custom non-UCI commands, useful for debugging
            else
            {
                if (token == "bench")
                {
                    bench(iss, pos, states);
                }
                else
                if (token == "flip")
                {
                    pos.flip();
                }
                else
                if (token == "mirror")
                {
                    pos.mirror();
                }
                else
                if (token == "compiler")
                {
                    sync_cout << compilerInfo() << sync_endl;
                }
                // Print the root position
                else
                if (token == "show")
                {
                    sync_cout << pos << sync_endl;
                }
                else
                if (token == "eval")
                {
                    sync_cout << trace(pos) << sync_endl;
                }
                // Print the root fen and keys
                else
                if (token == "keys")
                {
                    sync_cout << hex << uppercase << setfill('0')
                              << "FEN: "                  << pos.fen()        << "\n"
                              << "Posi key: " << setw(16) << pos.posiKey() << "\n"
                              << "Matl key: " << setw(16) << pos.matlKey() << "\n"
                              << "Pawn key: " << setw(16) << pos.pawnKey() << "\n"
                              << "PG key: "   << setw(16) << pos.pgKey()
                              << setfill(' ') << nouppercase << dec << sync_endl;
                }
                else
                if (token == "moves")
                {
                    sync_cout;
                    i32 count;
                    if (0 != pos.checkers())
                    {
                        cout << "\nEvasion moves: ";
                        count = 0;
                        for (const auto &vm : MoveList<GenType::EVASION>(pos))
                        {
                            if (pos.legal(vm))
                            {
                                cout << sanMove(vm, pos) << " ";
                                ++count;
                            }
                        }
                        cout << "(" << count << ")";
                    }
                    else
                    {
                        cout << "\nQuiet moves: ";
                        count = 0;
                        for (const auto &vm : MoveList<GenType::QUIET>(pos))
                        {
                            if (pos.legal(vm))
                            {
                                cout << sanMove(vm, pos) << " ";
                                ++count;
                            }
                        }
                        cout << "(" << count << ")";

                        cout << "\nCheck moves: ";
                        count = 0;
                        for (const auto &vm : MoveList<GenType::CHECK>(pos))
                        {
                            if (pos.legal(vm))
                            {
                                cout << sanMove(vm, pos) << " ";
                                ++count;
                            }
                        }
                        cout << "(" << count << ")";

                        cout << "\nQuiet Check moves: ";
                        count = 0;
                        for (const auto &vm : MoveList<GenType::QUIET_CHECK>(pos))
                        {
                            if (pos.legal(vm))
                            {
                                cout << sanMove(vm, pos) << " ";
                                ++count;
                            }
                        }
                        cout << "(" << count << ")";

                        cout << "\nCapture moves: ";
                        count = 0;
                        for (const auto &vm : MoveList<GenType::CAPTURE>(pos))
                        {
                            if (pos.legal(vm))
                            {
                                cout << sanMove(vm, pos) << " ";
                                ++count;
                            }
                        }
                        cout << "(" << count << ")";
                    }

                    cout << "\nLegal moves: ";
                    count = 0;
                    for (const auto &vm : MoveList<GenType::LEGAL>(pos))
                    {
                        cout << sanMove(vm, pos) << " ";
                        ++count;
                    }
                    cout << "(" << count << ")" << sync_endl;
                }
                else
                {
                    sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl;
                }
            }
        } while (1 == argc && cmd != "quit");
    }
}

/// run() runs with command arguments
void run(u32 argc, const char *const *argv)
{
    cout << Name << " " << engineInfo() << " by " << Author << endl;
    cout << "info string Processor(s) detected " << thread::hardware_concurrency() << endl;

    BitBoard::initialize();
    BitBases::initialize();
    initializePSQ();
    initializeZobrist();
    Position::initialize();
    UCI::initialize();
    Endgames::initialize();
    WinProcGroup::initialize();
    Threadpool.configure(optionThreads());
    Book.initialize(string(Options["Book File"]));
    Searcher::initialize();
    Searcher::clear();

    processInput(argc, argv);
}

/// stop() exits with a code (in case of some crash).
void stop(i32 code)
{
    Threadpool.stop = true;
    Threadpool.configure(0);
    std::exit(code);
}
