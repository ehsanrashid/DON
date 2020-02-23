#include "UCI.h"

#include <cassert>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

#include "Helper.h"
#include "Debugger.h"
#include "Evaluator.h"
#include "Polyglot.h"
#include "Position.h"
#include "Logger.h"
#include "MoveGenerator.h"
#include "Notation.h"
#include "Thread.h"
#include "TimeManager.h"
#include "Transposition.h"
#include "Searcher.h"
#include "SkillManager.h"
#include "SyzygyTB.h"

using std::istringstream;
using std::ostringstream;
using std::string;
using std::vector;

// Engine Name
string const Name{ "DON" };
// Version number. If version is left empty, then show compile date in the format YY-MM-DD.
string const Version{ "" };
// Author Name
string const Author{ "Ehsan Rashid" };

UCI::StringOptionMap Options;

namespace {

    Array<string, 12> const Months{ "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    i32 month(string const &mmm) {
        // for (u32 m = 0; m < Months.size(); ++m) {
        //     if (mmm == Months[m]) {
        //         return m+1;
        //     }
        // }
        // return 0;
        auto itr{ std::find(Months.begin(), Months.end(), mmm) };
        return itr != Months.end() ?
                i32(std::distance(Months.begin(), itr)) + 1 : 0;
    }
}

#define STRINGIFY(x)                    #x
#define STRING(x)                       STRINGIFY(x)
#define VER_STRING(major, minor, patch) STRING(major) "." STRING(minor) "." STRING(patch)

/// engineInfo() returns a string trying to describe the engine
string const engineInfo() {
    ostringstream oss;

    oss << std::setfill('0');
#if defined(VER)
    oss << VER;
#else
    if (whiteSpaces(Version)) {
        // From compiler, format is "Sep 2 1982"
        istringstream iss{ __DATE__ };
        string mmm, dd, yyyy;
        iss >> mmm >> dd >> yyyy;
        oss << std::setw(2) << yyyy.substr(2)
            << std::setw(2) << month(mmm)
            << std::setw(2) << dd;
    }
    else {
        oss << Version;
    }
#endif
    oss << std::setfill(' ');

#if defined(BIT64)
    oss << ".64";
#else
    oss << ".32";
#endif

#if defined(BM2)
    oss << ".BM2";
#elif defined(ABM)
    oss << ".ABM";
#endif

    return oss.str();
}
/// compilerInfo() returns a string trying to describe the compiler used
string const compilerInfo() {
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

#undef STRINGIFY
#undef STRING
#undef VER_STRING

namespace UCI {

    Option::Option(OnChange pFn)
        : type{ "button" }
        , onChange{ pFn } {}
    Option::Option(bool v, OnChange pFn)
        : type{ "check" }
        , onChange{ pFn } {
        defaultVal = currentVal = (v ? "true" : "false");
    }
    Option::Option(char const *v, OnChange pFn)
        : Option{ string{ v }, pFn } {}
    Option::Option(string const &v, OnChange pFn)
        : type{ "string" }
        , onChange{ pFn } {
        defaultVal = currentVal = v;
    }
    Option::Option(double v, double minV, double maxV, OnChange pFn)
        : type{ "spin" }
        , minVal{ minV }
        , maxVal{ maxV }
        , onChange{ pFn } {
        defaultVal = currentVal = std::to_string(v);
    }
    Option::Option(char const* v, char const* cur, OnChange pFn)
        : Option{ string{ v }, string{ cur }, pFn }
    {}
    Option::Option(string const& v, string const& cur, OnChange pFn)
        : type{ "combo" }
        , onChange{ pFn } {
        defaultVal = v; currentVal = cur;
    }

    Option::operator std::string() const {
        assert(type == "string");
        return currentVal;
    }
    Option::operator bool() const {
        assert(type == "check");
        return currentVal == "true";
    }
    Option::operator i16() const {
        assert(type == "spin");
        return i16(std::stoi(currentVal));
    }
    Option::operator u16() const {
        assert(type == "spin");
        return u16(std::stoi(currentVal));
    }
    Option::operator i32() const {
        assert(type == "spin");
        return i32(std::stoi(currentVal));
    }
    Option::operator u32() const {
        assert(type == "spin");
        return u32(std::stoi(currentVal));
    }
    Option::operator i64() const {
        assert(type == "spin");
        return i64(std::stoi(currentVal)); //std::stol(currentVal);
    }
    Option::operator u64() const {
        assert(type == "spin");
        return u64(std::stoi(currentVal)); //std::stol(currentVal);
    }
    Option::operator double() const {
        assert(type == "spin");
        return std::stod(currentVal);
    }

    bool Option::operator==(char const *v) const {
        return *this == string{ v };
    }
    bool Option::operator==(string const &v) const {
        assert(type == "combo");
        return !CaseInsensitiveLessComparer()(currentVal, v)
            && !CaseInsensitiveLessComparer()(v, currentVal);
    }

    Option& Option::operator=(char const *v) {
        *this = string{ v };
        return *this;
    }
    /// Option::operator=() updates currentValue and triggers onChange() action
    Option& Option::operator=(string &v) {
        assert(!type.empty());

        if (type == "check") {
            toLower(v);
            if (v != "true" && v != "false") {
                v = "false";
            }
        }
        else if (type == "spin") {
            auto d = std::stod(v);
            if (minVal > d || d > maxVal) {
                v = std::to_string(i32(clamp(d, minVal, maxVal)));
            }
        }
        else if (type == "string") {
            if (whiteSpaces(v)) {
                v.clear();
            }
        }
        else if (type == "combo") {
            StringOptionMap comboMap; // To have case insensitive compare
            istringstream iss{ defaultVal };
            string token;
            while (iss >> token) {
                comboMap[token] << Option();
            }
            if (comboMap.find(v) == comboMap.end()
             || v == "var") {
                return *this;
            }
        }

        if (type != "button") currentVal = v;
        if (nullptr != onChange) onChange();

        return *this;
    }

    /// Option::operator<<() inits options and assigns idx in the correct printing order
    void Option::operator<<(Option const &opt) {
        static u32 insertOrder = 0;

        *this = opt;
        index = insertOrder++;
    }

    /// Option::toString()
    string Option::toString() const
    {
        ostringstream oss;
        oss << " type " << type;

        if (type == "string"
         || type == "check"
         || type == "combo") {
            oss << " default " << defaultVal;
                //<< " current " << currentVal;
        }
        else if (type == "spin") {
            oss << " default " << i32(std::stod(defaultVal))
                << " min " << minVal
                << " max " << maxVal;
                //<< " current " << std::stod(currentVal);
        }

        return oss.str();
    }

    std::ostream& operator<<(std::ostream &os, Option const &opt) {
        os << opt.toString();
        return os;
    }

    /// This is used to print all the options default values in chronological
    /// insertion order and in the format defined by the UCI protocol.
    std::ostream& operator<<(std::ostream &os, StringOptionMap const &strOptMap) {
        for (size_t idx = 0; idx < strOptMap.size(); ++idx) {
            for (auto &strOptPair : strOptMap) {
                if (strOptPair.second.index == idx) {
                    os  << "option name "
                        << strOptPair.first
                        << strOptPair.second
                        << std::endl;
                }
            }
        }
        return os;
    }

}
namespace UCI {

    /// 'On change' actions, triggered by an option's value change

    namespace {

        void onHash() {
            TT.autoResize(Options["Hash"]);
        }

        void onClearHash() {
            UCI::clear();
        }

        void onSaveHash() {
            TT.save(Options["Hash File"]);
        }
        void onLoadHash() {
            TT.load(Options["Hash File"]);
        }

        void onBookFile() {
            Book.initialize(Options["Book.bin"]);
        }

        void onThreads() {
            auto threadCount{ optionThreads() };
            if (threadCount != Threadpool.size()) {
                Threadpool.configure(threadCount);
            }
        }

        void onTimeNodes() {
            TimeMgr.reset();
        }

        void onDebugFile() {
            Logger::instance().setFile(Options["Debug File"]);
        }

        void onSyzygyPath() {
            SyzygyTB::initialize(Options["SyzygyPath"]);
        }

    }

    void initialize() {

        Options["Hash"]               << Option(16, 0, TTable::MaxHashSize, onHash);

        Options["Clear Hash"]         << Option(onClearHash);
        Options["Retain Hash"]        << Option(false);

        Options["Hash File"]          << Option("Hash.dat");
        Options["Save Hash"]          << Option(onSaveHash);
        Options["Load Hash"]          << Option(onLoadHash);

        Options["Use Book"]           << Option(false);
        Options["Book File"]          << Option("Book.bin", onBookFile);
        Options["Book Pick Best"]     << Option(true);
        Options["Book Move Num"]      << Option(20, 0, 100);

        Options["Threads"]            << Option(1, 0, 512, onThreads);

        Options["Skill Level"]        << Option(MaxLevel,  0, MaxLevel);

        Options["MultiPV"]            << Option( 1, 1, 500);

        Options["Fixed Contempt"]     << Option(  0, -100, 100);
        Options["Contempt Time"]      << Option( 40, 0, 1000);
        Options["Contempt Value"]     << Option(100, 0, 1000);
        Options["Analysis Contempt"]  << Option("Both var Off var White var Black var Both", "Both");

        Options["Draw MoveCount"]     << Option(50, 5, 50);

        Options["Overhead MoveTime"]  << Option(30,  0, 5000);
        Options["Minimum MoveTime"]   << Option(20,  0, 5000);
        Options["Move Slowness"]      << Option(84, 10, 1000);
        Options["Ponder"]             << Option(true);
        Options["Time Nodes"]         << Option( 0,  0, 10000, onTimeNodes);

        Options["SyzygyPath"]         << Option("", onSyzygyPath);
        Options["SyzygyDepthLimit"]   << Option(1, 1, 100);
        Options["SyzygyPieceLimit"]   << Option(7, 0, 7);
        Options["SyzygyMove50Rule"]   << Option(true);

        Options["Debug File"]         << Option("", onDebugFile);

        Options["UCI_Chess960"]       << Option(false);
        Options["UCI_AnalyseMode"]    << Option(false);
        Options["UCI_LimitStrength"]  << Option(false);
        Options["UCI_Elo"]            << Option(1350, 1350, 3100);

    }


    namespace {

        /// Forsyth-Edwards Notation (FEN) is a standard notation for describing a particular board position of a chess game.
        /// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.
        string const StartFEN{ "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" };

        vector<string> const DefaultCmds
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


        /// setoption() updates the UCI option ("name") to the given value ("value").
        void setOption(istringstream &iss) {
            string token;
            iss >> token; // Consume "name" token

            //if (token != "name") return;
            string name;
            // Read option-name (can contain spaces)
            while ((iss >> token)
                && token != "value") {// Consume "value" token if any
                name += (name.empty() ? "" : " ") + token;
            }

            //if (token != "value") return;
            string value;
            // Read option-value (can contain spaces)
            while (iss >> token) {
                value += (value.empty() ? "" : " ") + token;
            }

            if (Options.find(name) != Options.end()) {
                Options[name] = value;
                sync_cout << "info string option " << name << " = " << value << sync_endl;
            }
            else { sync_cout << "No such option: \'" << name << "\'" << sync_endl; }
        }

        /// position() sets up the starting position ("startpos")/("fen <fenstring>") and then
        /// makes the moves given in the move list ("moves") also saving the moves on stack.
        void position(istringstream &iss, Position &pos, StateListPtr &states) {
            string token;
            iss >> token; // Consume "startpos" or "fen" token

            string fen;
            if (token == "startpos") {
                fen = StartFEN;
                iss >> token; // Consume "moves" token if any
            }
            else { //if (token == "fen") // Can ignore this condition
                while (iss >> token
                    && token != "moves") {
                    fen += token + " ";
                    token.clear();
                }
                //assert(isOk(fen));
            }
            //else { return; }
            assert(token == "" || token == "moves");

            // Drop old and create a new one
            states = StateListPtr{ new std::deque<StateInfo>(1) };
            pos.setup(fen, states->back(), Threadpool.mainThread());
            //assert(pos.fen() == trim(fen));

            u16 count = 0;
            // Parse and validate moves (if any)
            while (iss >> token) {
                ++count;
                auto m = moveOfCAN(token, pos);
                //if (MOVE_NONE == m) {
                //    std::cerr << "ERROR: Illegal Move '" << token << "' at " << count << std::endl;
                //    return;
                //}

                states->emplace_back();
                pos.doMove(m, states->back());
            }
        }

        /// go() sets the thinking time and other parameters from the input string, then starts the search.
        void go(istringstream &iss, Position &pos, StateListPtr &states) {
            Threadpool.stop = true;
            Threadpool.mainThread()->waitIdle();

            Limits.clear();
            Limits.startTime = now(); // As early as possible!

            string token;
            while (iss >> token) {
                if (whiteSpaces(token))        { continue; }
                else if (token == "wtime")     { iss >> Limits.clock[WHITE].time; }
                else if (token == "btime")     { iss >> Limits.clock[BLACK].time; }
                else if (token == "winc")      { iss >> Limits.clock[WHITE].inc; }
                else if (token == "binc")      { iss >> Limits.clock[BLACK].inc; }
                else if (token == "movestogo") { iss >> Limits.movestogo; }
                else if (token == "movetime")  { iss >> Limits.moveTime; }
                else if (token == "depth")     { iss >> Limits.depth; }
                else if (token == "nodes")     { iss >> Limits.nodes; }
                else if (token == "mate")      { iss >> Limits.mate; }
                else if (token == "infinite")  { Limits.infinite = true; }
                else if (token == "ponder")    { Limits.ponder = true; }
                else if (token == "searchmoves") {
                    // Parse and Validate search-moves (if any)
                    while (iss >> token) {
                        auto m = moveOfCAN(token, pos);
                        //if (MOVE_NONE == m) {
                        //    std::cerr << "ERROR: Illegal Rootmove '" << token << "'" << std::endl;
                        //    continue;
                        //}
                        Limits.searchMoves.push_back(m);
                    }
                }
                else if (token == "ignoremoves") {
                    // Parse and Validate ignore-moves (if any)
                    for (auto const &vm : MoveList<GenType::LEGAL>(pos)) {
                        Limits.searchMoves.push_back(vm);
                    }
                    while (iss >> token) {
                        auto m = moveOfCAN(token, pos);
                        //if (MOVE_NONE == m) {
                        //    std::cerr << "ERROR: Illegal Rootmove '" << token << "'" << std::endl;
                        //    continue;
                        //}
                        Limits.searchMoves.erase(std::remove(Limits.searchMoves.begin()
                                                           , Limits.searchMoves.end(), m)
                                               , Limits.searchMoves.end());
                    }
                }
                else {
                    std::cerr << "Unknown token : " << token << std::endl;
                }
            }
            Threadpool.startThinking(pos, states);
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
        vector<string> setupBench(istringstream &iss, Position const &pos) {
            string token;

            // Assign default values to missing arguments
            string    hash{ (iss >> token) && !whiteSpaces(token) ? token : "16" };
            string threads{ (iss >> token) && !whiteSpaces(token) ? token : "1" };
            string   value{ (iss >> token) && !whiteSpaces(token) ? token : "13" };
            string    mode{ (iss >> token) && !whiteSpaces(token) ? token : "depth" };
            string     fen{ (iss >> token) && !whiteSpaces(token) ? token : "default" };

            vector<string> cmds;
            vector<string> uciCmds;

            if (whiteSpaces(fen))      { return uciCmds; }
            else if (fen == "current") { cmds.push_back(pos.fen()); }
            else if (fen == "default") { cmds = DefaultCmds; }
            else {
                std::ifstream ifs{ fen, std::ios::in };
                if (!ifs.is_open()) {
                    std::cerr << "ERROR: unable to open file ... \'" << fen << "\'" << std::endl;
                    return uciCmds;
                }
                string cmd;
                while (std::getline(ifs, cmd, '\n')) {
                    if (!whiteSpaces(cmd)) {
                        cmds.push_back(cmd);
                    }
                }
                ifs.close();
            }

            bool chess960{ Options["UCI_Chess960"] };

            uciCmds.push_back("setoption name Threads value " + threads);
            uciCmds.push_back("setoption name Hash value " + hash);
            uciCmds.push_back("ucinewgame");

            for (auto const &cmd : cmds) {
                if (cmd.find("setoption") != string::npos) {
                    uciCmds.push_back(cmd);
                }
                else {
                    uciCmds.push_back("position fen " + cmd);
                    /**/ if (mode == "eval")    uciCmds.push_back(mode);
                    else if (mode == "perft")   uciCmds.push_back(mode + " " + value);
                    else                        uciCmds.push_back("go " + mode + " " + value);
                }
            }

            if (fen != "current") {
                uciCmds.push_back("setoption name UCI_Chess960 value " + string{ chess960 ? "true" : "false" });
                uciCmds.push_back("position fen " + pos.fen());
            }
            return uciCmds;
        }

        /// bench() setup list of UCI commands is setup according to bench parameters,
        /// then it is run one by one printing a summary at the end.
        void bench(istringstream &iss, Position &pos, StateListPtr &states) {
            auto const uciCmds = setupBench(iss, pos);
            auto const count = u16(std::count_if(uciCmds.begin(), uciCmds.end(),
                                                [](string const &s) {
                                                    return 0 == s.find("eval")
                                                        || 0 == s.find("perft ")
                                                        || 0 == s.find("go ");
                                                }));
            Debugger::reset();

            auto elapsed{ now() };
            u16 i{ 0 };
            u64 nodes{ 0 };
            for (auto const &cmd : uciCmds) {
                istringstream is{ cmd };
                string token;
                is >> std::skipws >> token;

                if (whiteSpaces(token))         { continue; }
                else if (token == "setoption")  { setOption(is); }
                else if (token == "position")   { position(is, pos, states); }
                else if (token == "eval"
                      || token == "perft"
                      || token == "go")         {
                    std::cerr
                        << "\n---------------\n"
                        << "Position: "
                        << std::right
                        << std::setw(2) << ++i << '/' << count << " "
                        << std::left
                        << pos.fen() << std::endl;

                    /**/ if (token == "eval")   { sync_cout << Evaluator::trace(pos) << sync_endl; }
                    else if (token == "perft")  {
                        Depth depth{ 1 }; is >> depth; depth = std::max(Depth(1), depth);
                        perft<true>(pos, depth);
                    }
                    else if (token == "go")     {
                        go(is, pos, states);
                        Threadpool.mainThread()->waitIdle();
                        nodes += Threadpool.sum(&Thread::nodes);
                    }
                }
                else if (token == "ucinewgame") {
                    UCI::clear();
                    elapsed = now();
                }
                else {
                    std::cerr << "Unknown command: \'" << token << "\'" << std::endl;
                }
            }

            elapsed = now() - elapsed + 1; // Ensure positivity to avoid a 'divide by zero'

            Debugger::print(); // Just before exiting

            ostringstream oss;
            oss << std::right
                << "\n=================================\n"
                << "Total time (ms) :" << std::setw(16) << elapsed << "\n"
                << "Nodes searched  :" << std::setw(16) << nodes << "\n"
                << "Nodes/second    :" << std::setw(16) << nodes * 1000 / elapsed
                << "\n---------------------------------\n";
            std::cerr << oss.str() << std::endl;
        }
    }

    /// handleCommands() waits for a command from stdin, parses it and calls the appropriate function.
    /// Also intercepts EOF from stdin to ensure gracefully exiting if the GUI dies unexpectedly.
    /// Single command line arguments is executed once and returns immediately, e.g. 'bench'.
    /// In addition to the UCI ones, also some additional commands are supported.
    void handleCommands(string const &cmdLine) {

        Debugger::reset();

        Position pos;
        // Stack to keep track of the position states along the setup moves
        // (from the start position to the position just before the search starts).
        // Needed by 'draw by repetition' detection.
        StateListPtr states{ new std::deque<StateInfo>(1) };
        pos.setup(StartFEN, states->back(), Threadpool.mainThread());

        string cmd;
        do {
            if (cmdLine.empty()) {
                // Block here waiting for input or EOF
                if (!std::getline(std::cin, cmd, '\n')) {
                    cmd = "quit";
                }
            }
            else {
                cmd = cmdLine;
            }

            istringstream iss{ cmd };
            string token;
            iss >> std::skipws >> token;

            if (whiteSpaces(token))         { continue; }
            else if (token == "quit"
                  || token == "stop")       { Threadpool.stop = true; }
            // GUI sends 'ponderhit' to tell that the opponent has played the expected move.
            // So 'ponderhit' will be sent if told to ponder on the same move the opponent has played.
            // Now should continue searching but switch from pondering to normal search.
            else if (token == "ponderhit")  { Threadpool.mainThread()->ponder = false; } // Switch to normal search
            else if (token == "isready")    { sync_cout << "readyok" << sync_endl; }
            else if (token == "uci")        {
                sync_cout << "id name "     << Name << " " << engineInfo() << "\n"
                          << "id author "   << Author << "\n"
                          << Options
                          << "uciok" << sync_endl;
            }
            else if (token == "ucinewgame") { UCI::clear(); }
            else if (token == "position")   { position(iss, pos, states); }
            else if (token == "go")         { go(iss, pos, states); }
            else if (token == "setoption")  { setOption(iss); }
            // Additional custom non-UCI commands, useful for debugging
            // Do not use these commands during a search!
            else if (token == "bench")      { bench(iss, pos, states); }
            else if (token == "flip")       { pos.flip(); }
            else if (token == "mirror")     { pos.mirror(); }
            else if (token == "compiler")   { sync_cout << compilerInfo() << sync_endl; }
            else if (token == "show")       { sync_cout << pos << sync_endl; }
            else if (token == "eval")       { sync_cout << Evaluator::trace(pos) << sync_endl; }
            else if (token == "perft")      {
                Depth depth{ 1 };     iss >> depth; depth = std::max(Depth(1), depth);
                bool detail{ false }; iss >> std::boolalpha >> detail;
                perft<true>(pos, depth, detail);
            }
            else if (token == "keys")       {
                ostringstream oss;
                oss << "FEN: " << pos.fen() << "\n"
                    << std::hex << std::uppercase << std::setfill('0')
                    << "Posi key: " << std::setw(16) << pos.posiKey() << "\n"
                    << "Matl key: " << std::setw(16) << pos.matlKey() << "\n"
                    << "Pawn key: " << std::setw(16) << pos.pawnKey() << "\n"
                    << "PG key: "   << std::setw(16) << pos.pgKey();
                sync_cout << oss.str() << sync_endl;
            }
            else if (token == "moves")      {
                i32 count;

                if (0 == pos.checkers()) {

                    std::cout << "\nQuiet moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::QUIET>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")";

                    std::cout << "\nCheck moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::CHECK>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")";

                    std::cout << "\nQuiet Check moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::QUIET_CHECK>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")";

                    std::cout << "\nCapture moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::CAPTURE>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")";

                    std::cout << "\nNatural moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::NATURAL>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")" << std::endl;
                }
                else {
                    std::cout << "\nEvasion moves: ";
                    count = 0;
                    for (auto const &vm : MoveList<GenType::EVASION>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++count;
                        }
                    }
                    std::cout << "(" << count << ")" << std::endl;
                }
            }
            else { sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl; }

        } while (cmdLine.empty()
              && cmd != "quit");
    }

    /// clear() clear all stuff
    void clear() {
        Threadpool.stop = true;
        Threadpool.mainThread()->waitIdle();

        Threadpool.clear();
        TT.clear();
        TimeMgr.availableNodes = 0;

        SyzygyTB::initialize(Options["SyzygyPath"]); // Free up mapped files
    }

}

u16 optionThreads() {
    u16 threadCount{ Options["Threads"] };
    if (0 == threadCount) {
        threadCount = u16(std::thread::hardware_concurrency());
    }
    return threadCount;
}
