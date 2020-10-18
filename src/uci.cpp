#include "uci.h"

#include <cassert>
#include <algorithm>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>

#include "polyglot.h"
#include "position.h"
#include "evaluator.h"
#include "movegenerator.h"
#include "notation.h"
#include "thread.h"
#include "timemanager.h"
#include "transposition.h"
#include "searcher.h"
#include "skillmanager.h"
#include "syzygytb.h"
#include "helper/string.h"
#include "helper/string_view.h"
#include "helper/container.h"
#include "helper/logger.h"
#include "helper/reporter.h"

using std::string;
using std::string_view;
using std::vector;
using std::istringstream;
using std::ostringstream;

// Engine Name
string const Name{ "DON" };
// Version number. If version is left empty, then show compile date in the format YY-MM-DD.
string const Version{ "" };
// Author Name
string const Author{ "Ehsan Rashid" };

UCI::OptionMap Options;

std::optional<Logger> StdLogger;

namespace {

    string const Months[12] { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

    int32_t month(string const &mmm) {
        // for (uint32_t m = 0; m < 12; ++m) {
        //     if (mmm == Months[m]) {
        //         return m+1;
        //     }
        // }
        // return 0;
        auto const itr{ std::find(std::begin(Months), std::end(Months), mmm) };
        return int32_t(itr != std::end(Months) ? std::distance(std::begin(Months), itr) + 1 : 0);
    }
}


/// engineInfo() returns a string trying to describe the engine
string const engineInfo() {
    ostringstream oss;

    oss << std::setfill('0');
#if defined(USE_VERSION)
    oss << USE_VERSION;
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

    return oss.str();
}
/// compilerInfo() returns a string trying to describe the compiler used
string const compilerInfo() {
    ostringstream oss;
    oss << "\nCompiled by ";

#define VER_STRING(major, minor, patch) STRINGIFY(major) "." STRINGIFY(minor) "." STRINGIFY(patch)

#if defined(__clang__)
    oss << "clang++ " << VER_STRING(__clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__INTEL_COMPILER)
    oss << "Intel compiler " << "(version " STRINGIFY(__INTEL_COMPILER) " update " STRINGIFY(__INTEL_COMPILER_UPDATE) ")";
#elif defined(_MSC_VER)
    oss << "MSVC " << "(version " STRINGIFY(_MSC_FULL_VER) "." STRINGIFY(_MSC_BUILD) ")";
#elif defined(__GNUC__)
    oss << "g++ (GNUC) " << VER_STRING(__GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
    oss << "Unknown compiler " << "(unknown version)";
#endif

#if defined(__APPLE__)
    oss << " on Apple";
#elif defined(__CYGWIN__)
    oss << " on Cygwin";
#elif defined(__MINGW64__)
    oss << " on MinGW64";
#elif defined(__MINGW32__)
    oss << " on MinGW32";
#elif defined(_WIN64)
    oss << " on Microsoft Windows 64-bit";
#elif defined(_WIN32)
    oss << " on Microsoft Windows 32-bit";
#elif defined(__ANDROID__)
    oss << " on Android";
#elif defined(__linux__)
    oss << " on Linux";
#else
    oss << " on unknown system";
#endif

    oss << "\nCompilation settings include: ";
#if defined(IS_64BIT)
    oss << " 64bit";
#else
    oss << " 32bit";
#endif

#if defined(USE_VNNI)
    oss << " VNNI";
#endif
#if defined(USE_AVX512)
    oss << " AVX512";
#endif
#if defined(USE_PEXT)
    oss << " BMI2";
#endif
#if defined(USE_AVX2)
    oss << " AVX2";
#endif
#if defined(USE_SSE41)
    oss << " SSE41";
#endif
#if defined(USE_SSSE3)
    oss << " SSSE3";
#endif
#if defined(USE_SSE2)
    oss << " SSE2";
#endif
#if defined(USE_POPCNT)
    oss << " POPCNT";
#endif
#if defined(USE_MMX)
    oss << " MMX";
#endif
#if defined(USE_NEON)
    oss << " NEON";
#endif

#if !defined(NDEBUG)
    oss << " DEBUG";
#endif

    oss << "\n__VERSION__ macro expands to: ";
#if defined(__VERSION__)
    oss << __VERSION__;
#else
    oss << "(undefined macro)";
#endif
    oss << '\n';

#undef VER_STRING

    return oss.str();
}

namespace UCI {

    Option::Option(OnChange onCng) :
        type{ "button" },
        onChange{ onCng } {
    }
    Option::Option(bool v, OnChange onCng) :
        type{ "check" },
        onChange{ onCng } {
        defaultVal = currentVal = ::toString(v);
    }
    Option::Option(string_view v, OnChange onCng) :
        type{ "string" },
        onChange{ onCng } {
        defaultVal = currentVal = v;
    }
    Option::Option(double v, double minV, double maxV, OnChange onCng) :
        type{ "spin" },
        minVal{ minV },
        maxVal{ maxV },
        onChange{ onCng } {
        defaultVal = currentVal = std::to_string(v);
    }
    Option::Option(string_view v, string_view cur, OnChange onCng) :
        type{ "combo" },
        onChange{ onCng } {
        defaultVal = v; currentVal = cur;
    }

    //Option::operator std::string() const {
    //    assert(type == "string");
    //    return currentVal;
    //}
    Option::operator std::string_view() const noexcept {
        assert(type == "string");
        return currentVal;
    }
    Option::operator     bool() const noexcept {
        assert(type == "check");
        return currentVal == "true";
    }
    Option::operator  int16_t() const noexcept {
        assert(type == "spin");
        return  int16_t( std::stoi(currentVal) );
    }
    Option::operator uint16_t() const noexcept {
        assert(type == "spin");
        return uint16_t( std::stoi(currentVal) );
    }
    Option::operator  int32_t() const noexcept {
        assert(type == "spin");
        return  int32_t( std::stoi(currentVal) );
    }
    Option::operator uint32_t() const noexcept {
        assert(type == "spin");
        return uint32_t( std::stoi(currentVal) );
    }
    Option::operator  int64_t() const noexcept {
        assert(type == "spin");
        return  int64_t( std::stoi(currentVal) ); //std::stol(currentVal);
    }
    Option::operator uint64_t() const noexcept {
        assert(type == "spin");
        return uint64_t( std::stoi(currentVal) ); //std::stol(currentVal);
    }
    Option::operator   double() const noexcept {
        assert(type == "spin");
        return( std::stod(currentVal) );
    }

    bool Option::operator==(string_view v) const {
        assert(type == "combo");
        return !CaseInsensitiveLessComparer()(currentVal, v)
            && !CaseInsensitiveLessComparer()(v, currentVal);
    }

    /// Option::operator=() updates currentValue and triggers onChange() action
    Option& Option::operator=(string_view v) {
        assert(!type.empty());
        string val{ v };
        if (type == "check") {
            val = toLower(val);
            if (val != "true" && val != "false") {
                val = "false";
            }
        }
        else if (type == "spin") {
            auto const d = std::stod(val);
            if (minVal > d || d > maxVal) {
                val = std::to_string(int32_t(std::clamp(d, minVal, maxVal)));
            }
        }
        else if (type == "string") {
            if (whiteSpaces(val)) {
                val.clear();
            }
        }
        else if (type == "combo") {
            istringstream iss{ defaultVal };
            OptionMap comboMap; // To have case insensitive compare
            string token;
            while (iss >> token) {
                comboMap[token] << Option();
            }
            if (!contains(comboMap, val)
             || val == "var") {
                return *this;
            }
        }

        if (type != "button") {
            currentVal = val;
        }
        if (onChange != nullptr) {
            onChange(*this);
        }
        return *this;
    }

    /// Option::operator<<() inits options and assigns idx in the correct printing order
    void Option::operator<<(Option const &opt) {
        static uint32_t insertOrder = 0;

        *this = opt;
        index = insertOrder++;
    }

    const string& Option::defaultValue() const noexcept {
        return defaultVal;
    }

    /// Option::toString()
    string Option::toString() const {
        ostringstream oss;
        oss << " type " << type;

        if (type == "string"
         || type == "check"
         || type == "combo") {
            oss << " default " << defaultVal;
                //<< " current " << currentVal;
        }
        else if (type == "spin") {
            oss << " default " << int32_t(std::stof(defaultVal))
                << " min " << int32_t(minVal)
                << " max " << int32_t(maxVal);
                //<< " current " << int32_t(std::stod(currentVal));
        }

        return oss.str();
    }

    std::ostream& operator<<(std::ostream &ostream, Option const &opt) {
        ostream << opt.toString();
        return ostream;
    }

    /// This is used to print all the options default values in chronological
    /// insertion order and in the format defined by the UCI protocol.
    std::ostream& operator<<(std::ostream &ostream, OptionMap const &om) {
        for (size_t idx = 0; idx < om.size(); ++idx) {
            for (auto &strOptPair : om) {
                if (strOptPair.second.index == idx) {
                    ostream << "option name " << strOptPair.first << strOptPair.second << '\n';
                }
            }
        }
        return ostream;
    }

}
namespace UCI {

    /// 'On change' actions, triggered by an option's value change

    namespace {

        void onHash(Option const &o) noexcept {
            TT.autoResize(uint32_t(o));
            TTEx.autoResize(uint32_t(o)/4);
        }

        void onClearHash(Option const&) noexcept {
            UCI::clear();
        }

        void onSaveHash(Option const&) noexcept {
            TT.save(Options["Hash File"]);
        }
        void onLoadHash(Option const&) noexcept {
            TT.load(Options["Hash File"]);
        }

        void onBookFile(Option const &o) noexcept {
            Book.initialize(o);
        }

        void onThreads(Option const&) noexcept {
            auto const threadCount{ optionThreads() };
            //if (threadCount != Threadpool.size()) {
            Threadpool.setup(threadCount);
            //}
        }

        void onTimeNodes(Option const&) noexcept {
            TimeMgr.clear();
        }

        void onLogFile(Option const &o) noexcept {
            if (!StdLogger) {
                StdLogger.emplace(std::cin, std::cout); // Tie std::cin and std::cout to a file.
            }
            StdLogger.value().setup(o);
        }

        void onSyzygyPath(Option const &o) noexcept {
            SyzygyTB::initialize(o);
        }

        void onUseNNUE(Option const&) noexcept {
            Evaluator::NNUE::initialize();
        }
        void onEvalFile(Option const&) noexcept {
            Evaluator::NNUE::initialize();
        }
    }

    void initialize() noexcept {

        Options["Hash"]               << Option(16, TTable::MinHashSize, TTable::MaxHashSize, onHash);

        Options["Clear Hash"]         << Option(onClearHash);
        Options["Retain Hash"]        << Option(false);

        Options["Hash File"]          << Option(string("Hash.dat"));
        Options["Save Hash"]          << Option(onSaveHash);
        Options["Load Hash"]          << Option(onLoadHash);

        Options["Use Book"]           << Option(false);
        Options["Book File"]          << Option(string("Book.bin"), onBookFile);
        Options["Book Pick Best"]     << Option(true);
        Options["Book Move Num"]      << Option(20, 0, 100);

        Options["Threads"]            << Option(1, 0, 512, onThreads);

        Options["Skill Level"]        << Option(MaxLevel,  0, MaxLevel);

        Options["MultiPV"]            << Option( 1, 1, 500);

        Options["Fixed Contempt"]     << Option(  0, -100, 100);
        Options["Contempt Time"]      << Option( 40,    0, 1000);
        Options["Contempt Value"]     << Option(100,    0, 1000);
        Options["Analysis Contempt"]  << Option(string("Both var Off var White var Black var Both"), string("Both"));

        Options["Draw MoveCount"]     << Option(50, 5, 50);

        Options["Overhead MoveTime"]  << Option( 10,  0, 5000);
        Options["Move Slowness"]      << Option(100, 10, 1000);
        Options["Ponder"]             << Option(true);
        Options["Time Nodes"]         << Option( 0,  0, 10000, onTimeNodes);

        Options["SyzygyPath"]         << Option(string(""), onSyzygyPath);
        Options["SyzygyDepthLimit"]   << Option(1, 1, 100);
        Options["SyzygyPieceLimit"]   << Option(SyzygyTB::TBPIECES, 0, SyzygyTB::TBPIECES);
        Options["SyzygyMove50Rule"]   << Option(true);

        Options["Use NNUE"]           << Option(true, onUseNNUE);

#if defined(_MSC_VER)
        Options["Eval File"]          << Option(string("src/") + DefaultEvalFile, onEvalFile);
#else
        Options["Eval File"]          << Option(string("") + DefaultEvalFile, onEvalFile);
#endif

        Options["Log File"]           << Option(string(""), onLogFile);

        Options["UCI_Chess960"]       << Option(false);
        Options["UCI_ShowWDL"]        << Option(false);
        Options["UCI_AnalyseMode"]    << Option(false);
        Options["UCI_LimitStrength"]  << Option(false);
        Options["UCI_Elo"]            << Option(1350, 1350, 3100);

    }

    namespace {

        /// Forsyth-Edwards Notation (FEN) is a standard notation for describing a particular board position of a chess game.
        /// The purpose of FEN is to provide all the necessary information to restart a game from a particular position.
        string const StartFEN{ "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1" };

        vector<string> const DefaultFens{
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
            "4k3/3q1r2/1N2r1b1/3ppN2/2nPP3/1B1R2n1/2R1Q3/3K4 w - - 5 1",

            // 4-man positions
            "8/6k1/5r2/8/8/8/1K6/Q7 w - - 0 1"        // Kc3 - mate in 27

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
            "setoption name UCI_Chess960 value false"
        };

        // trace_eval() prints the evaluation for the current position, consistent with the UCI
        // options set so far.

        void traceEval(Position &pos) {
            StateListPtr states{ new StateList{ 1 } };
            Position cPos;
            cPos.setup(pos.fen(), states->back(), Threadpool.mainThread());

            Evaluator::NNUE::verify();

            sync_cout << '\n' << Evaluator::trace(cPos) << sync_endl;
        }

        /// setoption() updates the UCI option ("name") to the given value ("value").
        void setOption(istringstream &iss, Position &pos) {
            string token;
            iss >> token; // Consume "name" token

            //if (token != "name") return;
            string name;
            // Read option-name (can contain spaces)
            while (iss >> token) { // Consume "value" token if any
                if (token == "value") break;
                name += (name.empty() ? "" : " ") + token;
            }

            //if (token != "value") return;
            string value;
            // Read option-value (can contain spaces)
            while (iss >> token) {
                value += (value.empty() ? "" : " ") + token;
            }

            if (contains(Options, name)) {
                Options[name] = value;
                sync_cout << "info string option " << name << " = " << value << sync_endl;
                if (pos.thread() != Threadpool.mainThread()) {
                    pos.thread(Threadpool.mainThread());
                }
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
            else
            if (token == "fen") {
                while (iss >> token) { // Consume "moves" token if any
                    if (token == "moves") break;
                    fen += token + " ";
                }
                //assert(isOk(fen));
            }
            else { return; }

            // Drop old and create a new one
            states = StateListPtr{ new StateList{ 1 } };
            pos.setup(fen, states->back(), Threadpool.mainThread());
            //assert(pos.fen() == toString(trim(fen)));

            // Parse and validate moves (if any)
            while (iss >> token) {
                auto const m{ moveOfCAN(token, pos) };
                if (m == MOVE_NONE) {
                    std::cerr << "ERROR: Illegal Move '" << token << "' at " << iss.tellg() << '\n';
                    break;
                }

                states->emplace_back();
                pos.doMove(m, states->back());
            }
        }

        /// go() sets the thinking time and other parameters from the input string, then starts the search.
        void go(istringstream &iss, Position &pos, StateListPtr &states) {
            Threadpool.stop = true;
            Threadpool.mainThread()->waitIdle();
            Threadpool.ponder = false;

            Limits.clear();
            TimeMgr.startTime = now(); // As early as possible!

            string token;
            while (iss >> token) {
                     if (token == "wtime")     { iss >> Limits.clock[WHITE].time; }
                else if (token == "btime")     { iss >> Limits.clock[BLACK].time; }
                else if (token == "winc")      { iss >> Limits.clock[WHITE].inc; }
                else if (token == "binc")      { iss >> Limits.clock[BLACK].inc; }
                else if (token == "movestogo") { iss >> Limits.movestogo; }
                else if (token == "movetime")  { iss >> Limits.moveTime; }
                else if (token == "depth")     { iss >> Limits.depth; }
                else if (token == "nodes")     { iss >> Limits.nodes; }
                else if (token == "mate")      { iss >> Limits.mate; }
                else if (token == "infinite")  { Limits.infinite = true; }
                else if (token == "ponder")    { Threadpool.ponder = true; }
                // Needs to be the last command on the line
                else if (token == "searchmoves") {
                    // Parse and Validate search-moves (if any)
                    while (iss >> token) {
                        auto const m{ moveOfCAN(token, pos) };
                        if (m == MOVE_NONE) {
                            std::cerr << "ERROR: Illegal Rootmove '" << token << "'\n";
                            continue;
                        }
                        Limits.searchMoves += m;
                    }
                }
                else if (token == "ignoremoves") {
                    // Parse and Validate ignore-moves (if any)
                    for (auto const &vm : MoveList<LEGAL>(pos)) {
                        Limits.searchMoves += vm;
                    }
                    while (iss >> token) {
                        auto const m{ moveOfCAN(token, pos) };
                        if (m == MOVE_NONE) {
                            std::cerr << "ERROR: Illegal Rootmove '" << token << "'\n";
                            continue;
                        }
                        if (Limits.searchMoves.contains(m)) {
                            Limits.searchMoves -= m;
                        }
                    }
                }
                //else {
                //    std::cerr << "Unknown token : " << token << '\n';
                //}
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
        /// - Evaluation type
        ///     * classical (default)
        ///     * nnue
        ///     * mixed
        /// example:
        /// bench -> search default positions up to depth 13
        /// bench 256 4 10 depth default classical -> search default positions up to depth 10 using classical evaluation
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
            string   limit{ (iss >> token) && !whiteSpaces(token) ? toLower(token) : "depth" };
            string fenFile{ (iss >> token) && !whiteSpaces(token) ? toLower(token) : "default" };
            string    eval{ (iss >> token) && !whiteSpaces(token) ? toLower(token) : "classical" };

            string command{
                limit == "eval"  ? limit :
                limit == "perft" ? limit + " " + value :
                                   "go " + limit + " " + value };

            vector<string> fens;
                 if (fenFile == "default") { fens = DefaultFens; }
            else if (fenFile == "current") { fens.push_back(pos.fen()); }
            else {
                std::ifstream ifstream{ fenFile, std::ios::in };
                if (ifstream.is_open()) {
                    string fen;
                    while (std::getline(ifstream, fen, '\n')) {
                        if (!whiteSpaces(fen)) {
                            fens.push_back(fen);
                        }
                    }
                    ifstream.close();
                }
                else {
                    std::cerr << "ERROR: unable to open file ... \'" << fenFile << "\'\n";
                }
            }

            bool uciChess960{ Options["UCI_Chess960"] };

            vector<string> uciCmds;
            uciCmds.emplace_back("setoption name Threads value " + threads);
            uciCmds.emplace_back("setoption name Hash value " + hash);
            uciCmds.emplace_back("ucinewgame");

            if (eval == "classical") {
                uciCmds.emplace_back("setoption name Use NNUE value false");
            }
            else
            if (eval == "nnue") {
                uciCmds.emplace_back("setoption name Use NNUE value true");
            }

            uint32_t posCount{ 0 };
            for (auto const &fen : fens) {
                if (fen.find("setoption") != string::npos) {
                    uciCmds.emplace_back(fen);
                }
                else {
                    if (eval == "mixed") {
                        uciCmds.emplace_back(string("setoption name Use NNUE value ") + (posCount % 2 != 0 ? "true" : "false"));
                    }

                    uciCmds.emplace_back("position fen " + fen);
                    uciCmds.emplace_back(command);

                    ++posCount;
                }
            }

            if (fenFile != "current") {
                uciCmds.emplace_back("setoption name UCI_Chess960 value " + toString(uciChess960));
                uciCmds.emplace_back("position fen " + pos.fen());
            }
            uciCmds.emplace_back("setoption name Use NNUE value " + Options["Use NNUE"].defaultValue());

            return uciCmds;
        }

        /// bench() setup list of UCI commands is setup according to bench parameters,
        /// then it is run one by one printing a summary at the end.
        void bench(istringstream &isstream, Position &pos, StateListPtr &states) {

            auto const uciCmds{ setupBench(isstream, pos) };
            auto const cmdCount{ std::count_if(uciCmds.begin(), uciCmds.end(),
                                            [](string const &s) {
                                                return s.find("eval") == 0
                                                    || s.find("perft ") == 0
                                                    || s.find("go ") == 0;
                                            }) };

            Reporter::reset();
            TimePoint elapsed{ now() };
            uint64_t nodes{ 0 };
            int32_t i{ 0 };
            for (auto const &cmd : uciCmds) {
                istringstream iss{ cmd };
                string token;
                iss >> std::skipws >> token;

                     if (token == "eval"
                      || token == "perft"
                      || token == "go") {

                    std::cerr << "\n---------------\nPosition: "
                              << std::right << std::setw(2) << ++i << '/' << cmdCount << " (" << std::left << pos.fen() << ")\n";

                         if (token == "eval") {
                        traceEval(pos);
                    }
                    else if (token == "perft") {
                        Depth depth{ 1 };
                        iss >> depth; depth = std::max(Depth(1), depth);

                        perft<true>(pos, depth);
                    }
                    else if (token == "go") {
                        go(iss, pos, states);
                        Threadpool.mainThread()->waitIdle();
                        nodes += Threadpool.accumulate(&Thread::nodes);
                    }
                }
                else if (token == "setoption")  { setOption(iss, pos); }
                else if (token == "position")   { position(iss, pos, states); }
                else if (token == "ucinewgame") { UCI::clear(); elapsed = now(); }
            }

            elapsed = std::max(now() - elapsed, { 1 }); // Ensure non-zero to avoid a 'divide by zero'

            Reporter::print(); // Just before exiting

            ostringstream oss;
            oss << std::right
                << "\n=================================\n"
                << "Total time (ms) :" << std::setw(16) << elapsed << '\n'
                << "Nodes searched  :" << std::setw(16) << nodes << '\n'
                << "Nodes/second    :" << std::setw(16) << nodes * 1000 / elapsed
                << "\n---------------------------------\n";
            std::cerr << oss.str() << '\n';
        }
    }

    /// handleCommands() waits for a command from stdin, parses it and calls the appropriate function.
    /// Also intercepts EOF from stdin to ensure gracefully exiting if the GUI dies unexpectedly.
    /// Single command line arguments is executed once and returns immediately, e.g. 'bench'.
    /// In addition to the UCI ones, also some additional commands are supported.
    void handleCommands(int argc, char const *const argv[]) {

        Position pos;
        // Stack to keep track of the position states along the setup moves
        // (from the start position to the position just before the search starts).
        // Needed by 'draw by repetition' detection.
        StateListPtr states{ new StateList{ 1 } };
        pos.setup(StartFEN, states->back(), Threadpool.mainThread());

        // Join arguments
        string cmd;
        for (int i = 1; i < argc; ++i) {
            cmd += string(argv[i]) + " ";
        }

        Reporter::reset();
        string token;
        do {
            // Block here waiting for input or EOF
            if (argc == 1
                // Default endline '\n'
             && !std::getline(std::cin, cmd)) {
                cmd = "quit";
            }

            istringstream iss{ cmd };
            token.clear(); // Avoid a stale if getline() returns empty or blank line
            iss >> std::skipws >> token;
            token = toLower(token);

                 if (token == "quit"
                  || token == "stop")       { Threadpool.stop = true; }
            // GUI sends 'ponderhit' to tell that the opponent has played the expected move.
            // So 'ponderhit' will be sent if told to ponder on the same move the opponent has played.
            // Now should continue searching but switch from pondering to normal search.
            else if (token == "ponderhit")  { Threadpool.ponder = false; } // Switch to normal search
            else if (token == "isready")    { sync_cout << "readyok" << sync_endl; }
            else if (token == "uci")        {
                sync_cout << "id name "     << Name << " " << engineInfo() << '\n'
                          << "id author "   << Author << '\n'
                          << Options
                          << "uciok" << sync_endl;
            }
            else if (token == "ucinewgame") { UCI::clear(); }
            else if (token == "position")   { position(iss, pos, states); }
            else if (token == "go")         { go(iss, pos, states); }
            else if (token == "setoption")  { setOption(iss, pos); }
            // Additional custom non-UCI commands, useful for debugging
            // Do not use these commands during a search!
            else if (token == "bench")      { bench(iss, pos, states); }
            else if (token == "flip")       { pos.flip(); }
            else if (token == "mirror")     { pos.mirror(); }
            else if (token == "compiler")   { sync_cout << compilerInfo() << sync_endl; }
            else if (token == "show")       { sync_cout << pos << sync_endl; }
            else if (token == "eval")       { traceEval(pos); }
            else if (token == "perft")      {
                Depth depth{ 1 };
                iss >> depth; depth = std::max(Depth(1), depth);
                bool detail{ false };
                iss >> std::boolalpha >> detail;

                perft<true>(pos, depth, detail);
            }
            else if (token == "keys")       {
                ostringstream oss;
                oss << "FEN: " << pos.fen() << '\n'
                    << std::hex << std::uppercase << std::setfill('0')
                    << "Posi key: " << std::setw(16) << pos.posiKey() << '\n'
                    << "Matl key: " << std::setw(16) << pos.matlKey() << '\n'
                    << "Pawn key: " << std::setw(16) << pos.pawnKey() << '\n'
                    << "PG key: "   << std::setw(16) << pos.pgKey();
                sync_cout << oss.str() << sync_endl;
            }
            else if (token == "moves")      {
                sync_cout;
                int32_t moveCount;
                std::cout << '\n';
                if (pos.checkers() == 0) {
                    std::cout << "Capture moves: ";
                    moveCount = 0;
                    for (auto const &vm : MoveList<CAPTURE>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++moveCount;
                        }
                    }
                    std::cout << "(" << moveCount << ")\n";

                    std::cout << "Quiet moves: ";
                    moveCount = 0;
                    for (auto const &vm : MoveList<QUIET>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++moveCount;
                        }
                    }
                    std::cout << "(" << moveCount << ")\n";

                    std::cout << "Quiet Check moves: ";
                    moveCount = 0;
                    for (auto const &vm : MoveList<QUIET_CHECK>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++moveCount;
                        }
                    }
                    std::cout << "(" << moveCount << ")\n";

                    std::cout << "Natural moves: ";
                    moveCount = 0;
                    for (auto const &vm : MoveList<NORMAL>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++moveCount;
                        }
                    }
                    std::cout << "(" << moveCount << ")\n";
                }
                else {
                    std::cout << "Evasion moves: ";
                    moveCount = 0;
                    for (auto const &vm : MoveList<EVASION>(pos)) {
                        if (pos.pseudoLegal(vm)
                         && pos.legal(vm)) {
                            std::cout << moveToSAN(vm, pos) << " ";
                            ++moveCount;
                        }
                    }
                    std::cout << "(" << moveCount << ")\n";
                }
                std::cout << sync_endl;
            }
            else {
                sync_cout << "Unknown command: \'" << cmd << "\'" << sync_endl;
            }

        } while (argc == 1
              && token != "quit");
    }

    /// clear() clear all stuff
    void clear() noexcept {
        Threadpool.stop = true;
        Threadpool.mainThread()->waitIdle();

        TT.clear();
        TTEx.clear();
        TimeMgr.clear();
        Threadpool.clean();

        SyzygyTB::initialize(Options["SyzygyPath"]); // Free up mapped files
    }

}

uint16_t optionThreads() {
    uint16_t threadCount{ Options["Threads"] };
    if (threadCount == 0) {
        threadCount = uint16_t(std::thread::hardware_concurrency());
    }
    return threadCount;
}
