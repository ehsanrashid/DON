/*
  DON, a UCI chess playing engine derived from Stockfish

  DON is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  DON is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "uci.h"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <utility>
#include <vector>

#include "benchmark.h"
#include "bitboard.h"
#include "memory.h"
#include "position.h"
#include "search.h"
#include "ucioption.h"

namespace DON {

namespace {

enum Command : std::uint8_t {
    CMD_STOP,
    CMD_QUIT,
    CMD_PONDERHIT,
    CMD_POSITION,
    CMD_GO,
    CMD_SETOPTION,
    CMD_UCI,
    CMD_UCINEWGAME,
    CMD_ISREADY,
    // Add custom non-UCI commands, mainly for debugging purposes.
    // These commands must not be used during a search!
    CMD_BENCH,
    CMD_BENCHMARK,
    CMD_SHOW,
    CMD_DUMP,
    CMD_EVAL,
    CMD_FLIP,
    CMD_MIRROR,
    CMD_COMPILER,
    CMD_EXPORT_NET,
    CMD_HELP,
    // Unknown Command
    CMD_NONE,
};

// clang-format off
const std::unordered_map<std::string_view, Command> CommandMap{
  {"stop",       CMD_STOP},
  {"quit",       CMD_QUIT},
  {"ponderhit",  CMD_PONDERHIT},
  {"position",   CMD_POSITION},
  {"go",         CMD_GO},
  {"setoption",  CMD_SETOPTION},
  {"uci",        CMD_UCI},
  {"ucinewgame", CMD_UCINEWGAME},
  {"isready",    CMD_ISREADY},
  {"bench",      CMD_BENCH},
  {"benchmark",  CMD_BENCHMARK},
  {"show",       CMD_SHOW},
  {"dump",       CMD_DUMP},
  {"eval",       CMD_EVAL},
  {"flip",       CMD_FLIP},
  {"mirror",     CMD_MIRROR},
  {"compiler",   CMD_COMPILER},
  {"export_net", CMD_EXPORT_NET},
  {"--help",     CMD_HELP},
  {"help",       CMD_HELP},
  {"--license",  CMD_HELP},
  {"license",    CMD_HELP}};
// clang-format on

Command str_to_command(std::string_view command) noexcept {
    auto itr = CommandMap.find(command);
    return itr != CommandMap.end() ? itr->second : CMD_NONE;
}

Limit parse_limit(std::istream& is) noexcept {
    Limit limit;
    // The search starts as early as possible
    limit.startTime = now();

    std::string token;
    while (is >> token)
    {
        token = lower_case(token);

        if (token == "wtime")
        {
            is >> limit.clocks[WHITE].time;
            limit.clocks[WHITE].time = std::max(std::abs(limit.clocks[WHITE].time), TimePoint(1));
        }
        else if (token == "btime")
        {
            is >> limit.clocks[BLACK].time;
            limit.clocks[BLACK].time = std::max(std::abs(limit.clocks[BLACK].time), TimePoint(1));
        }
        else if (token == "winc")
        {
            is >> limit.clocks[WHITE].inc;
            limit.clocks[WHITE].inc = std::max(std::abs(limit.clocks[WHITE].inc), TimePoint(1));
        }
        else if (token == "binc")
        {
            is >> limit.clocks[BLACK].inc;
            limit.clocks[BLACK].inc = std::max(std::abs(limit.clocks[BLACK].inc), TimePoint(1));
        }
        else if (token == "movetime")
        {
            is >> limit.moveTime;
            limit.moveTime = std::max(std::abs(limit.moveTime), TimePoint(1));
        }
        else if (token == "movestogo")
        {
            std::int16_t movesToGo;
            is >> movesToGo;
            limit.movesToGo =
              std::clamp(std::abs(movesToGo), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "mate")
        {
            std::int16_t mate;
            is >> mate;
            limit.mate = std::clamp(std::abs(mate), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "depth")
        {
            is >> limit.depth;
            limit.depth = std::clamp(std::abs(limit.depth), 1, MAX_PLY - 1);
        }
        else if (token == "nodes")
        {
            is >> limit.nodes;
            limit.nodes = std::max(limit.nodes, std::uint64_t(1));
        }
        else if (token == "infinite")
            limit.infinite = true;
        else if (token == "ponder")
            limit.ponder = true;
        else if (token == "perft")
        {
            limit.perft = true;
            is >> limit.depth;
            limit.depth = std::clamp(std::abs(limit.depth), 1, MAX_PLY - 1);
            is >> std::boolalpha >> limit.detail;
        }
        // "searchmoves" needs to be the last command on the line
        else if (token.size() >= 1 && token[0] == 's')  // "searchmoves"
        {
            auto pos = is.tellg();
            while (is >> token && !(token.size() >= 1 && std::tolower(token[0]) == 'i'))
            {
                limit.searchMoves.push_back(token);
                pos = is.tellg();
            }
            is.seekg(pos);
        }
        // "ignoremoves" needs to be the last command on the line
        else if (token.size() >= 1 && token[0] == 'i')  // "ignoremoves"
        {
            auto pos = is.tellg();
            while (is >> token && !(token.size() >= 1 && std::tolower(token[0]) == 's'))
            {
                limit.ignoreMoves.push_back(token);
                pos = is.tellg();
            }
            is.seekg(pos);
        }
    }
    return limit;
}

}  // namespace

UCI::UCI(int argc, const char* argv[]) noexcept :
    engine(argv[0]),
    commandLine(argc, argv) {

    options().set_info_listener([](const std::optional<std::string>& optStr) {
        if (optStr.has_value())
            print_info_string(*optStr);
    });

    set_update_listeners();
}

void UCI::run() noexcept {
    std::string command;
    command.reserve(256);
    for (std::size_t i = 1; i < commandLine.arguments.size(); ++i)
    {
        if (!command.empty())
            command.push_back(' ');
        command.append(commandLine.arguments[i].data(), commandLine.arguments[i].size());
    }

    const bool running = commandLine.arguments.size() <= 1;
    if (!running && is_whitespace(command))
        return;

    do
    {
        // The command-line arguments are one-shot
        if (running
            // Wait for an input or an end-of-file (EOF) indication
            && !std::getline(std::cin, command))
            command = "quit";

        execute(command);

        if (command == "quit")
            break;
    } while (running);
}

void UCI::execute(std::string_view command) noexcept {

    std::istringstream iss{std::string(command)};
    iss >> std::skipws;

    std::string token;
    iss >> token;
    if (token.empty())
        return;

    switch (str_to_command(lower_case(token)))
    {
    case CMD_STOP :
    case CMD_QUIT :
        engine.stop();
        break;
    case CMD_PONDERHIT :
        // The GUI sends 'ponderhit' to tell that the user has played the expected move.
        // So, 'ponderhit' is sent if pondering was done on the same move that the user has played.
        // The search should continue, but should also switch from pondering to the normal search.
        engine.ponderhit();
        break;
    case CMD_POSITION :
        position(iss);
        break;
    case CMD_GO :
        // Send info strings after the go command is sent for old GUIs and python-chess
        print_info_string(engine.get_numa_config_info_str());
        print_info_string(engine.get_thread_allocation_info_str());

        go(iss);
        break;
    case CMD_SETOPTION :
        setoption(iss);
        break;
    case CMD_UCI :
        std::cout << engine_info(true) << '\n'  //
                  << options() << '\n'          //
                  << "uciok" << std::endl;
        break;
    case CMD_UCINEWGAME :
        engine.init();
        break;
    case CMD_ISREADY :
        std::cout << "readyok" << std::endl;
        break;
    // Add custom non-UCI commands, mainly for debugging purposes.
    // These commands must not be used during a search!
    case CMD_BENCH :
        bench(iss);
        break;
    case CMD_BENCHMARK :
        benchmark(iss);
        break;
    case CMD_SHOW :
        engine.show();
        break;
    case CMD_DUMP : {
        std::optional<std::string> dumpFile;

        std::string input;
        if (iss >> input)
            dumpFile = input;

        engine.dump(dumpFile);
    }
    break;
    case CMD_EVAL :
        engine.eval();
        break;
    case CMD_FLIP :
        engine.flip();
        break;
    case CMD_MIRROR :
        engine.mirror();
        break;
    case CMD_COMPILER :
        std::cout << compiler_info() << '\n' << std::endl;
        break;
    case CMD_EXPORT_NET : {
        StdArray<std::optional<std::string>, 2> netFiles;

        std::string input;
        for (std::size_t i = 0; i < netFiles.size() && iss >> input; ++i)
            netFiles[i] = input;

        engine.save_networks(netFiles);
    }
    break;
    case CMD_HELP :
        std::cout
          << "\nDON is a powerful chess engine for playing and analyzing."
             "\nIt is released as free software licensed under the GNU GPLv3 License."
             "\nDON is normally used with a graphical user interface (GUI) and implements"
             "\nthe Universal Chess Interface (UCI) protocol to communicate with a GUI, an API, etc."
             "\nFor any further information, visit https://github.com/ehsanrashid/DON#readme"
             "\nor read the corresponding README.md and Copying.txt files distributed along with this program.\n"
          << std::endl;
        break;
    default :
        if (token[0] != '#')
        {
            std::cout << "\nUnknown command: '" << command << "'."
                      << "\nType help for more information." << std::endl;
        }
    }
}

void UCI::print_info_string(std::string_view infoStr) noexcept {
    if (InfoStringStop)
        return;

    for (const auto& str : split(infoStr, "\n"))
        if (!is_whitespace(str))
            std::cout << "info string " << str << std::endl;
}

namespace {

void on_update_short(const ShortInfo& sInfo) noexcept {
    std::cout << "info"                    //
              << " depth " << sInfo.depth  //
              << " score " << sInfo.score << std::endl;
}

void on_update_full(const FullInfo& fInfo) noexcept {
    std::cout << "info"                          //
              << " depth " << fInfo.depth        //
              << " seldepth " << fInfo.selDepth  //
              << " multipv " << fInfo.multiPV    //
              << " score " << fInfo.score;       //
    if (!fInfo.bound.empty())
        std::cout << fInfo.bound;
    if (!fInfo.wdl.empty())
        std::cout << " wdl " << fInfo.wdl;
    std::cout << " time " << fInfo.time                      //
              << " nodes " << fInfo.nodes                    //
              << " nps " << 1000 * fInfo.nodes / fInfo.time  //
              << " hashfull " << fInfo.hashfull              //
              << " tbhits " << fInfo.tbHits                  //
              << " pv" << fInfo.pv << std::endl;
}

void on_update_iter(const IterInfo& iInfo) noexcept {
    std::cout << "info"                          //
              << " depth " << iInfo.depth        //
              << " currmove " << iInfo.currMove  //
              << " currmovenumber " << iInfo.currMoveNumber << std::endl;
}

void on_update_move(const MoveInfo& mInfo) noexcept {
    std::cout << "bestmove " << mInfo.bestMove  //
              << " ponder " << mInfo.ponderMove << std::endl;
}

}  // namespace

void UCI::set_update_listeners() noexcept {
    engine.set_on_update_short(on_update_short);
    engine.set_on_update_full(on_update_full);
    engine.set_on_update_iter(on_update_iter);
    engine.set_on_update_move(on_update_move);
}

void UCI::position(std::istream& is) noexcept {

    std::string token;
    is >> token;
    token = lower_case(token);

    std::string fen;
    if (token.size() >= 1 && token[0] == 's')  // "startpos"
    {
        fen = START_FEN;
        is >> token;  // Consume the "moves" token, if any
    }
    else if (token.size() >= 1 && token[0] == 'f')  // "fen"
    {
        fen.reserve(64);
        std::size_t i = 0;
        while (is >> token && i < 6)  // Consume the "moves" token, if any
        {
            if (i >= 2 && token.size() >= 1 && std::tolower(token[0]) == 'm')  // "moves"
                break;

            fen += token;
            fen += ' ';
            ++i;
        }
        while (i < 4)
        {
            fen += "- ";
            ++i;
        }
    }
    else
    {
        assert(false && "Invalid position command");
        return;
    }

    Strings moves;
    while (is >> token)
        moves.push_back(token);

    engine.setup(fen, moves);
}

void UCI::go(std::istream& is) noexcept {
    auto limit = parse_limit(is);

    if (limit.perft)
        perft(limit.depth, limit.detail);
    else
    {
        engine.start(limit);
        // Not wait here
    }
}

void UCI::setoption(std::istream& is) noexcept {
    engine.wait_finish();

    std::string token;
    is >> token;  // Consume the "name" token
    assert(lower_case(token) == "name");

    // Read the option name (can contain spaces)
    std::string name;
    while (is >> token && lower_case(token) != "value")
    {
        if (!name.empty())
            name += ' ';
        name += token;
    }

    // Read the option value (can contain spaces)
    std::string value;
    while (is >> token)
    {
        if (!value.empty())
            value += ' ';
        value += token;
    }

    options().set(name, value);
}

void UCI::bench(std::istream& is) noexcept {

    auto commands = Benchmark::bench(is, engine.fen());

    std::uint64_t infoNodes = 0;
    engine.set_on_update_full([&infoNodes](const auto& info) {
        infoNodes = info.nodes;
        on_update_full(info);
    });

    auto reportMinimal = bool_to_string(options()["ReportMinimal"]);

    options().set("ReportMinimal", bool_to_string(true));

    const std::size_t num =
      std::count_if(commands.begin(), commands.end(), [](std::string_view command) {
          return starts_with(command, "go ") || starts_with(command, "eval");
      });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    std::size_t   cnt   = 0;
    std::uint64_t nodes = 0;

    for (const auto& command : commands)
    {
        std::istringstream iss{command};
        iss >> std::skipws;

        std::string token;
        iss >> token;
        if (token.empty())
            continue;

        switch (str_to_command(lower_case(token)))
        {
        case CMD_GO : {
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")"
                      << std::endl;

            auto limit = parse_limit(iss);

            if (limit.perft)
                infoNodes = perft(limit.depth, limit.detail);
            else
            {
                engine.start(limit);
                engine.wait_finish();
            }

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case CMD_EVAL :
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")"
                      << std::endl;
            engine.eval();
            break;
        case CMD_POSITION :
            position(iss);
            break;
        case CMD_SETOPTION :
            setoption(iss);
            break;
        case CMD_UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }
    }

    elapsedTime += now() - startTime;
    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime, TimePoint(1));

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << "\n================"                   //
              << "\nTotal time [ms] : " << elapsedTime  //
              << "\nTotal nodes     : " << nodes        //
              << "\nnodes/second    : " << 1000 * nodes / elapsedTime << std::endl;

    options().set("ReportMinimal", reportMinimal);
    // Reset callback, to not capture a dangling reference to infoNodes
    engine.set_on_update_full(on_update_full);
}

void UCI::benchmark(std::istream& is) noexcept {
    // Probably not very important for a test this long, but include for completeness and sanity.
    constexpr std::size_t WarmupPositionCount = 3;

    auto setup = Benchmark::benchmark(is);

    // Set options once at the start
    options().set("Threads", std::to_string(setup.threads));
    options().set("Hash", std::to_string(setup.ttSize));
    options().set("UCI_Chess960", bool_to_string(false));

    std::uint64_t infoNodes = 0;
    engine.set_on_update_short([](const auto&) {});
    engine.set_on_update_full([&](const auto& info) { infoNodes = info.nodes; });
    engine.set_on_update_iter([](const auto&) {});
    engine.set_on_update_move([](const auto&) {});

    InfoStringStop = true;

    const std::size_t num =
      std::count_if(setup.commands.begin(), setup.commands.end(),
                    [](std::string_view command) { return starts_with(command, "go "); });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    std::size_t   cnt   = 0;
    std::uint64_t nodes = 0;
    // Warmup
    for (const auto& command : setup.commands)
    {
        std::istringstream iss{command};
        iss >> std::skipws;

        std::string token;
        iss >> token;
        if (token.empty())
            continue;

        switch (str_to_command(lower_case(token)))
        {
        case CMD_GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rWarmup position " << ++cnt << '/' << WarmupPositionCount;

            auto limit = parse_limit(iss);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case CMD_POSITION :
            position(iss);
            break;
        case CMD_UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }

        if (cnt >= WarmupPositionCount)
            break;
    }

    std::cerr << '\n';

    cnt   = 0;
    nodes = 0;

    // Only normal hashfull and touched hash
    constexpr StdArray<std::uint8_t, 2> HashfullAges{0, 31};

    static_assert(HashfullAges.size() == 2 && HashfullAges[0] == 0 && HashfullAges[1] == 31,
                  "Incorrect HashfullAges[].");

    std::uint16_t                                hashfullCount{0};
    StdArray<std::uint16_t, HashfullAges.size()> maxHashfull{};
    StdArray<std::uint32_t, HashfullAges.size()> sumHashfull{};

    const auto update_hashfull = [&]() noexcept -> void {
        ++hashfullCount;
        for (std::size_t i = 0; i < HashfullAges.size(); ++i)
        {
            auto hashfull = engine.hashfull(HashfullAges[i]);
            if (maxHashfull[i] < hashfull)
                maxHashfull[i] = hashfull;
            sumHashfull[i] += hashfull;
        }
    };

    const auto avg = [&hashfullCount](std::uint32_t x) noexcept {
        return double(x) / hashfullCount;
    };

    elapsedTime += now() - startTime;
    engine.init();  // May take a while
    startTime = now();

    for (const auto& command : setup.commands)
    {
        std::istringstream iss{command};
        iss >> std::skipws;

        std::string token;
        iss >> token;
        if (token.empty())
            continue;

        switch (str_to_command(lower_case(token)))
        {
        case CMD_GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rPosition " << ++cnt << '/' << num;

            auto limit = parse_limit(iss);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();

            update_hashfull();

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case CMD_POSITION :
            position(iss);
            break;
        case CMD_UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }
    }

    elapsedTime += now() - startTime;
    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime, TimePoint(1));

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << '\n';

    auto threadBinding = engine.get_thread_binding_info_str();
    if (threadBinding.empty())
        threadBinding = "none";

    // clang-format off
    std::cerr << "\n==========================="
              << "\nVersion                    : " << version_info()
              << "\nCompiler                   : " << compiler_info()
              << "\nLarge pages                : " << bool_to_string(has_large_pages())
              << "\nOriginal invocation        : " << "benchmark " << setup.originalInvocation
              << "\nFilled invocation          : " << "benchmark " << setup.filledInvocation
              << "\nAvailable processors       : " << engine.get_numa_config_str()
              << "\nThread count               : " << setup.threads
              << "\nThread binding             : " << threadBinding
              << "\nTT size [MiB]              : " << setup.ttSize
              << "\nHash max, sum, avg [mille] : Count=" << hashfullCount
              << "\n    Single search          : " << maxHashfull[0] << ", " << sumHashfull[0] << ", " << avg(sumHashfull[0])
              << "\n    Single game            : " << maxHashfull[1] << ", " << sumHashfull[1] << ", " << avg(sumHashfull[1])
              << "\nTotal time [s]             : " << elapsedTime / 1000.0
              << "\nTotal nodes                : " << nodes
              << "\nnodes/second               : " << 1000 * nodes / elapsedTime << std::endl;
    // clang-format on

    InfoStringStop = false;
    set_update_listeners();
}

std::uint64_t UCI::perft(Depth depth, bool detail) noexcept {
    std::uint64_t nodes = engine.perft(depth, detail);
    std::cout << "\nTotal nodes: " << nodes << '\n' << std::endl;
    return nodes;
}

namespace {

struct WinRateParams final {
    double a, b;
};

WinRateParams win_rate_params(const Position& pos) noexcept {

    // clang-format off
    constexpr StdArray<double, 4> as{-13.50030198,   40.92780883, -36.82753545, 386.83004070};
    constexpr StdArray<double, 4> bs{ 96.53354896, -165.79058388,  90.89679019,  49.29561889};
    // clang-format on

    // The fitted model only uses data for material counts in [17, 78], and is anchored at count 58 (17.2414e-3).
    double m = 17.2414e-3 * std::clamp(pos.std_material(), 17, 78);
    // Return a = p_a(material) and b = p_b(material).
    double a = ((as[0] * m + as[1]) * m + as[2]) * m + as[3];
    double b = ((bs[0] * m + bs[1]) * m + bs[2]) * m + bs[3];

    return {a, b};
}

// The win rate model is 1 / (1 + exp((a - eval) / b)), where a = p_a(material) and b = p_b(material)
int win_rate_model(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    auto [a, b] = win_rate_params(pos);
    // Return the win rate in per mille units, rounded to the nearest integer
    return int(0.5 + 1000 / (1 + std::exp((a - v) / b)));
}

template<typename... Ts>
struct Overload final: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

}  // namespace

// Turns a Value to an integer centipawn number,
// without treatment of mate and similar special scores.
int UCI::to_cp(Value v, const Position& pos) noexcept {
    assert(is_ok(v));
    // In general, the score can be defined via the WDL as
    // (log(1/L - 1) - log(1/W - 1)) / (log(1/L - 1) + log(1/W - 1)).
    // Based on our win_rate_model, this simply yields v / a.

    auto [a, b] = win_rate_params(pos);

    return std::round(100 * int(v) / a);
}

std::string UCI::to_wdl(Value v, const Position& pos) noexcept {
    assert(is_ok(v));

    auto wdlW = win_rate_model(+v, pos);
    auto wdlL = win_rate_model(-v, pos);
    auto wdlD = 1000 - (wdlW + wdlL);

    std::string wdl;
    wdl.reserve(16);
    wdl += std::to_string(wdlW);
    wdl += ' ';
    wdl += std::to_string(wdlD);
    wdl += ' ';
    wdl += std::to_string(wdlL);
    return wdl;
}

std::string UCI::to_score(const Score& score) noexcept {
    constexpr int TB_CP = 20000;

    const auto format =
      Overload{[](Score::Unit unit) -> std::string {  //
                   return "cp " + std::to_string(unit.value);
               },
               [](Score::Tablebase tb) -> std::string {  //
                   return "cp " + std::to_string((tb.win ? +TB_CP : -TB_CP) - tb.ply);
               },
               [](Score::Mate mate) -> std::string {  //
                   return "mate " + std::to_string((mate.ply + (mate.ply > 0)) / 2);
               }};

    return score.visit(format);
}

std::string UCI::move_to_can(Move m) noexcept {
    if (m == Move::None)
        return "(none)";
    if (m == Move::Null)
        return "0000";

    Square orgSq = m.org_sq(), dstSq = m.dst_sq();

    if (!Position::Chess960 && m.type_of() == CASTLING)
    {
        assert(rank_of(orgSq) == rank_of(dstSq));
        dstSq = king_castle_sq(orgSq, dstSq);
    }

    std::string can;
    can.reserve(5);

    can += to_square(orgSq);
    can += to_square(dstSq);

    if (m.type_of() == PROMOTION)
        can += char(std::tolower(to_char(m.promotion_type())));

    return can;
}

// Converts a string representing a move in coordinate notation
// (g1f3, a7a8q) to the corresponding legal move, if any.
Move UCI::can_to_move(std::string can, const MoveList<LEGAL>& legalMoves) noexcept {
    assert(4 <= can.size() && can.size() <= 5);
    can = lower_case(can);

    for (auto m : legalMoves)
        if (can == move_to_can(m))
            return m;

    return Move::None;
}

Move UCI::can_to_move(std::string can, const Position& pos) noexcept {
    return can_to_move(can, MoveList<LEGAL>(pos));
}

namespace {

enum Ambiguity : std::uint8_t {
    AMB_NONE,
    AMB_RANK,
    AMB_FILE,
    AMB_SQUARE,
};

// Ambiguity if more then one piece of same type can reach 'to' with a legal move.
// NOTE: for pawns it is not needed because 'orgSq' file is explicit.
Ambiguity ambiguity(Move m, const Position& pos) noexcept {
    assert(pos.legal(m));

    Color ac = pos.active_color();

    Square orgSq = m.org_sq(), dstSq = m.dst_sq();
    assert(color_of(pos[orgSq]) == ac);
    PieceType movedPt = type_of(pos[orgSq]);

    // If there is only one piece then move cannot be ambiguous
    if (pos.count(ac, movedPt) == 1)
        return AMB_NONE;

    // Disambiguation if have more then one piece with same destination
    Bitboard movedBB =
      (attacks_bb(dstSq, movedPt, pos.pieces_bb()) & pos.pieces_bb(ac, movedPt)) ^ orgSq;

    if (!movedBB)
        return AMB_NONE;

    Bitboard b = movedBB;
    // If pinned piece is considered as ambiguous
    //& ~pos.blockers_bb(ac);
    while (b != 0)
    {
        Square sq = pop_lsq(b);

        Move mm = Move(sq, dstSq);
        if (!pos.legal(mm))
            movedBB ^= sq;
    }

    if ((movedBB & file_of(orgSq)) == 0)
        return AMB_RANK;
    if ((movedBB & rank_of(orgSq)) == 0)
        return AMB_FILE;

    return AMB_SQUARE;
}

}  // namespace

std::string UCI::move_to_san(Move m, Position& pos) noexcept {
    if (m == Move::None)
        return "(none)";
    if (m == Move::Null)
        return "0000";
    assert(MoveList<LEGAL>(pos).contains(m));

    Square orgSq = m.org_sq(), dstSq = m.dst_sq();
    assert(color_of(pos[orgSq]) == pos.active_color());

    auto movedPt = type_of(pos[orgSq]);

    std::string san;
    san.reserve(9);

    if (m.type_of() == CASTLING)
    {
        assert(movedPt == KING && rank_of(orgSq) == rank_of(dstSq));
        auto cs = castling_side(orgSq, dstSq);
        san     = (cs == KING_SIDE ? "O-O" : "O-O-O");
    }
    else
    {
        // Note:: For pawns is not needed because starting file is explicit.
        if (movedPt != PAWN)
        {
            san = to_char(movedPt);

            if (movedPt != KING)
            {
                // Disambiguation if have more then one piece of type 'pt'
                // that can reach 'dst' with any legal move.
                switch (ambiguity(m, pos))
                {
                case AMB_RANK :
                    san += to_char(file_of(orgSq));
                    break;
                case AMB_FILE :
                    san += to_char(rank_of(orgSq));
                    break;
                case AMB_SQUARE :
                    san += to_square(orgSq);
                    break;
                default :;
                }
            }
        }

        if (pos.capture(m))
        {
            if (movedPt == PAWN)
                san = to_char(file_of(orgSq));

            san += 'x';
        }

        san += to_square(dstSq);

        if (m.type_of() == PROMOTION)
        {
            assert(movedPt == PAWN);
            san += '=';
            san += char(std::toupper(to_char(m.promotion_type())));
        }
    }

    bool check = pos.check(m);

    State st;
    pos.do_move(m, st, check);

    // Marker for check, checkmate & stalemate
    const bool legalMovesEmpty = MoveList<LEGAL, true>(pos).empty();
    san += check ? (legalMovesEmpty ? '#' : '+') : (legalMovesEmpty ? '=' : '\0');

    pos.undo_move(m);

    return san;
}

Move UCI::san_to_move(std::string san, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept {
    assert(2 <= san.size() && san.size() <= 9);
    if (san.size() >= 2 && san[1] == '-' && (san[0] == '0' || std::tolower(san[0]) == 'o'))
        std::replace_if(san.begin(), san.end(), [](char c) { return c == 'o' || c == '0'; }, 'O');

    for (auto m : legalMoves)
        if (san == move_to_san(m, pos))
            return m;

    return Move::None;
}

Move UCI::san_to_move(std::string san, Position& pos) noexcept {
    return san_to_move(san, pos, MoveList<LEGAL>(pos));
}

Move UCI::mix_to_move(std::string mix, Position& pos, const MoveList<LEGAL>& legalMoves) noexcept {
    assert(2 <= mix.size() && mix.size() <= 9);
    Move m = Move::None;

    if (!legalMoves.empty() && mix.size() >= 2)
    {
        if (mix.size() <= 3 || (mix[1] == '-' && (mix[0] == '0' || std::tolower(mix[0]) == 'o')))
            return san_to_move(mix, pos, legalMoves);

        if (mix.size() <= 5)
            m = can_to_move(mix, legalMoves);

        if (m == Move::None && mix.size() <= 9)
            m = san_to_move(mix, pos, legalMoves);
    }

    return m;
}

}  // namespace DON
