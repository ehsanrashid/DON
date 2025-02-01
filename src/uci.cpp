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
#include <array>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "benchmark.h"
#include "evaluate.h"
#include "memory.h"
#include "movegen.h"
#include "position.h"
#include "score.h"
#include "search.h"

namespace DON {

namespace {

constexpr inline std::string_view              PieceChar{" PNBRQK  pnbrqk "};
const inline std::array<std::string, PIECE_NB> PieceFigure{
  "", "\u2659", "\u2658", "\u2657", "\u2656", "\u2655", "\u2654", "",
  "", "\u265F", "\u265E", "\u265D", "\u265C", "\u265B", "\u265A", ""};

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
    CMD_EVAL,
    CMD_FLIP,
    CMD_COMPILER,
    CMD_EXPORT_NET,
    CMD_HELP,
    // Unknown Command
    CMD_NONE,
};

// clang-format off
const inline std::unordered_map<std::string_view, Command> CommandMap{
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
  {"eval",       CMD_EVAL},
  {"flip",       CMD_FLIP},
  {"compiler",   CMD_COMPILER},
  {"export_net", CMD_EXPORT_NET},
  {"--help",     CMD_HELP},
  {"help",       CMD_HELP},
  {"--license",  CMD_HELP},
  {"license",    CMD_HELP}};
// clang-format on

template<typename... Ts>
struct Overload final: Ts... {
    using Ts::operator()...;
};

template<typename... Ts>
Overload(Ts...) -> Overload<Ts...>;

Command str_to_command(std::string_view command) noexcept {
    auto itr = CommandMap.find(command);
    return itr != CommandMap.end() ? itr->second : CMD_NONE;
}

Limit parse_limit(std::istringstream& iss) noexcept {

    Limit limit;
    // The search starts as early as possible
    limit.startTime = now();

    std::string token;
    while (iss >> token)
    {
        token = lower_case(token);

        if (token == "wtime")
        {
            iss >> limit.clocks[WHITE].time;
            limit.clocks[WHITE].time = std::max(std::abs(limit.clocks[WHITE].time), TimePoint(1));
        }
        else if (token == "btime")
        {
            iss >> limit.clocks[BLACK].time;
            limit.clocks[BLACK].time = std::max(std::abs(limit.clocks[BLACK].time), TimePoint(1));
        }
        else if (token == "winc")
        {
            iss >> limit.clocks[WHITE].inc;
            limit.clocks[WHITE].inc = std::max(std::abs(limit.clocks[WHITE].inc), TimePoint(1));
        }
        else if (token == "binc")
        {
            iss >> limit.clocks[BLACK].inc;
            limit.clocks[BLACK].inc = std::max(std::abs(limit.clocks[BLACK].inc), TimePoint(1));
        }
        else if (token == "movetime")
        {
            iss >> limit.moveTime;
            limit.moveTime = std::max(std::abs(limit.moveTime), TimePoint(1));
        }
        else if (token == "movestogo")
        {
            std::int16_t movesToGo;
            iss >> movesToGo;
            limit.movesToGo =
              std::clamp(std::abs(movesToGo), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "mate")
        {
            std::int16_t mate;
            iss >> mate;
            limit.mate = std::clamp(std::abs(mate), 1, +std::numeric_limits<std::uint8_t>::max());
        }
        else if (token == "depth")
        {
            iss >> limit.depth;
            limit.depth = std::clamp(std::abs(limit.depth), 1, MAX_PLY - 1);
        }
        else if (token == "nodes")
        {
            iss >> limit.nodes;
            limit.nodes = std::max(limit.nodes, 1ull);
            // When using nodes, ensure checking rate is not lower than 0.1% of nodes
            limit.hitRate = std::min(+limit.hitRate, 1 + int(std::ceil(limit.nodes / 1024.0)));
        }
        else if (token == "infinite")
            limit.infinite = true;
        else if (token == "ponder")
            limit.ponder = true;
        else if (token == "perft")
        {
            limit.perft = true;
            iss >> limit.depth;
            limit.depth = std::clamp(std::abs(limit.depth), 1, MAX_PLY - 1);
            iss >> std::boolalpha >> limit.detail;
        }
        // "searchmoves" needs to be the last command on the line
        else if (starts_with(token, "search"))
        {
            auto pos = iss.tellg();
            while (iss >> token)
            {
                if (starts_with(lower_case(token), "ignore"))
                {
                    iss.seekg(pos);
                    break;
                }
                limit.searchMoves.push_back(token);
                pos = iss.tellg();
            }
        }
        // "ignoremoves" needs to be the last command on the line
        else if (starts_with(token, "ignore"))
        {
            auto pos = iss.tellg();
            while (iss >> token)
            {
                if (starts_with(lower_case(token), "search"))
                {
                    iss.seekg(pos);
                    break;
                }
                limit.ignoreMoves.push_back(token);
                pos = iss.tellg();
            }
        }
    }
    return limit;
}

void on_update_end(const EndInfo& info) noexcept;
void on_update_full(const FullInfo& info) noexcept;
void on_update_iter(const IterInfo& info) noexcept;
void on_update_move(const MoveInfo& info) noexcept;

}  // namespace

bool UCI::infoStringStop = false;

UCI::UCI(int argc, const char** argv) noexcept :
    engine(argv[0]),
    commandLine(argc, argv) {

    engine_options().set_info_listener([](const std::optional<std::string>& infoOpt) {
        if (infoOpt.has_value())
            print_info_string(*infoOpt);
    });

    init_update_listeners();
}

void UCI::handle_commands() noexcept {
    std::string command;
    for (int i = 1; i < commandLine.argc; ++i)
    {
        if (i != 1)
            command += ' ';
        command += commandLine.argv[i];
    }
    // The command-line arguments are one-shot
    bool running = commandLine.argc <= 1;
    if (!running && is_whitespace(command))
        return;

    do
    {
        if (running
            // Wait for an input or an end-of-file (EOF) indication
            && !std::getline(std::cin, command))
            command = "quit";

        std::istringstream iss(command);
        iss >> std::skipws;

        std::string token;
        iss >> token;
        if (token.empty())
            continue;

        auto cmd = str_to_command(lower_case(token));
        switch (cmd)
        {
        case CMD_STOP :
        case CMD_QUIT :
            engine.stop();
            running &= cmd != CMD_QUIT;
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
            print_info_string(engine.get_numa_config_info());
            print_info_string(engine.get_thread_allocation_info());

            go(iss);
            break;
        case CMD_SETOPTION :
            set_option(iss);
            break;
        case CMD_UCI :
            std::cout << engine_info(true) << '\n'  //
                      << engine_options() << '\n'   //
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
        case CMD_EVAL :
            engine.eval();
            break;
        case CMD_FLIP :
            engine.flip();
            break;
        case CMD_COMPILER :
            std::cout << compiler_info() << '\n' << std::endl;
            break;
        case CMD_EXPORT_NET : {
            std::array<std::string, 2>                inputFiles;
            std::array<std::optional<std::string>, 2> files;

            if (iss >> inputFiles[0])
                files[0] = inputFiles[0];

            if (iss >> inputFiles[1])
                files[1] = inputFiles[1];

            engine.save_networks(files);
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
                std::cout << "Unknown command: '" << command << "'."
                          << "\nType help for more information." << std::endl;
            }
        }
    } while (running);
}

void UCI::print_info_string(std::string_view infoStr) noexcept {
    if (infoStringStop)
        return;

    for (const auto& info : split(infoStr, "\n"))
        if (!is_whitespace(info))
            std::cout << "info string " << info << std::endl;
}

void UCI::init_update_listeners() noexcept {
    engine.set_on_update_end(on_update_end);
    engine.set_on_update_full(on_update_full);
    engine.set_on_update_iter(on_update_iter);
    engine.set_on_update_move(on_update_move);
}

void UCI::position(std::istringstream& iss) noexcept {
    std::string fen;

    std::string token;
    iss >> token;
    token = lower_case(token);

    if (starts_with(token, "start"))  // "startpos"
    {
        fen = StartFEN;
        iss >> token;  // Consume the "moves" token, if any
    }
    else  //if (token == "fen")
    {
        std::uint8_t i = 0;
        while (iss >> token && i < 6)  // Consume the "moves" token, if any
        {
            if (i >= 2 && starts_with(lower_case(token), "moves"))
                break;

            fen += token + " ";
            ++i;
        }
        while (i < 4)
        {
            fen += "- ";
            ++i;
        }
    }

    std::vector<std::string> moves;
    while (iss >> token)
        moves.push_back(token);

    engine.setup(fen, moves);
}

void UCI::go(std::istringstream& iss) noexcept {
    auto limit = parse_limit(iss);

    if (limit.perft)
        engine.perft(limit.depth, limit.detail);
    else
        engine.start(limit);
}

void UCI::set_option(std::istringstream& iss) noexcept {
    std::string token, name, value;

    bool first;

    iss >> token;  // Consume the "name" token
    assert(lower_case(token) == "name");
    // Read the option name (can contain spaces)
    first = true;
    while (iss >> token && lower_case(token) != "value")
    {
        if (!first)
            name += ' ';
        name += token;
        first = false;
    }
    assert(lower_case(token) == "value");
    // Read the option value (can contain spaces)
    first = true;
    while (iss >> token)
    {
        if (!first)
            value += ' ';
        value += token;
        first = false;
    }

    engine_options().set_option(name, value);
}

void UCI::bench(std::istringstream& iss) noexcept {

    std::uint64_t infoNodes = 0;
    engine.set_on_update_full([&infoNodes](const auto& info) {
        infoNodes = info.nodes;
        on_update_full(info);
    });

    auto reportMinimal = bool_to_string(engine_options()["ReportMinimal"]);

    engine_options().set_option("ReportMinimal", bool_to_string(true));

#if !defined(NDEBUG)
    Debug::init();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    std::uint64_t nodes = 0;

    auto commands = Benchmark::setup_bench(iss, engine.fen());

    auto num = std::count_if(commands.begin(), commands.end(),  //
                             [](const auto& command) {          //
                                 return command.find("go ") == 0 || command.find("eval") == 0;
                             });

    std::size_t cnt = 0;
    for (const auto& command : commands)
    {
        std::istringstream is(command);

        std::string token;
        is >> std::skipws >> token;
        if (token.empty())
            continue;

        auto cmd = str_to_command(lower_case(token));
        switch (cmd)
        {
        case CMD_GO : {
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")\n";
            auto limit = parse_limit(is);

            if (limit.perft)
                infoNodes = engine.perft(limit.depth, limit.detail);
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
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")\n";
            engine.eval();
            break;
        case CMD_POSITION :
            position(is);
            break;
        case CMD_SETOPTION :
            set_option(is);
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

    std::cerr << "\n==========================="        //
              << "\nTotal time [ms] : " << elapsedTime  //
              << "\nTotal nodes     : " << nodes        //
              << "\nnodes/second    : " << 1000 * nodes / elapsedTime << std::endl;

    engine_options().set_option("ReportMinimal", reportMinimal);
    // Reset callback, to not capture a dangling reference to infoNodes
    engine.set_on_update_full(on_update_full);
}

void UCI::benchmark(std::istringstream& iss) noexcept {
    // Probably not very important for a test this long, but include for completeness and sanity.
    constexpr std::size_t WARMUP_POSITION_COUNT = 3;

    std::uint64_t infoNodes = 0;
    engine.set_on_update_full([&](const auto& info) { infoNodes = info.nodes; });
    engine.set_on_update_end([](const auto&) {});
    engine.set_on_update_iter([](const auto&) {});
    engine.set_on_update_move([](const auto&) {});

    auto benchmark = Benchmark::setup_benchmark(iss);

    auto num = std::count_if(benchmark.commands.begin(), benchmark.commands.end(),
                             [](const auto& command) {  //
                                 return command.find("go ") == 0;
                             });

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    // Set options once at the start.
    engine_options().set_option("Threads", std::to_string(benchmark.threads));
    engine_options().set_option("Hash", std::to_string(benchmark.ttSize));
    engine_options().set_option("UCI_Chess960", bool_to_string(false));

    infoStringStop = true;

    std::uint64_t nodes = 0;
    std::size_t   cnt   = 0;
    // Warmup
    for (const auto& command : benchmark.commands)
    {
        std::istringstream is(command);

        std::string token;
        is >> std::skipws >> token;
        if (token.empty())
            continue;

        auto cmd = str_to_command(lower_case(token));
        switch (cmd)
        {
        case CMD_GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rWarmup position " << ++cnt << '/' << WARMUP_POSITION_COUNT;

            auto limit = parse_limit(is);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case CMD_POSITION :
            position(is);
            break;
        case CMD_UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }

        if (cnt >= WARMUP_POSITION_COUNT)
            break;
    }

    elapsedTime += now() - startTime;
    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime, TimePoint(1));

    std::cerr << '\n';

    nodes = 0;
    cnt   = 0;

    constexpr std::uint8_t hashFullAges[2]{0, 31};  // Only normal hashfull and touched hash.

    std::uint16_t hashFullCount                        = 0;
    std::uint16_t maxHashFull[std::size(hashFullAges)] = {0};
    std::uint32_t sumHashFull[std::size(hashFullAges)] = {0};

    auto update_hashFull = [&]() {
        ++hashFullCount;
        for (std::size_t i = 0; i < std::size(hashFullAges); ++i)
        {
            auto hashFull  = engine.get_hashFull(hashFullAges[i]);
            maxHashFull[i] = std::max(maxHashFull[i], hashFull);
            sumHashFull[i] += hashFull;
        }
    };

    elapsedTime += now() - startTime;
    engine.init();  // May take a while
    startTime = now();

    for (const auto& command : benchmark.commands)
    {
        std::istringstream is(command);

        std::string token;
        is >> std::skipws >> token;
        if (token.empty())
            continue;

        auto cmd = str_to_command(lower_case(token));
        switch (cmd)
        {
        case CMD_GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rPosition " << ++cnt << '/' << num;

            auto limit = parse_limit(is);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();

            update_hashFull();

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case CMD_POSITION :
            position(is);
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

    static_assert(
      std::size(hashFullAges) == 2 && hashFullAges[0] == 0 && hashFullAges[1] == 31,
      "Hardcoded for display. Would complicate the code needlessly in the current state.");

    std::string threadBinding = engine.get_thread_binding_info();
    if (threadBinding.empty())
        threadBinding = "none";

    std::cerr << "\n==========================="
              << "\nVersion                    : " << version_info()
              << "\nCompiler                   : " << compiler_info()
              << "\nLarge pages                : " << bool_to_string(has_large_pages())
              << "\nOriginal invocation        : " << "benchmark " << benchmark.originalInvocation
              << "\nFilled invocation          : " << "benchmark " << benchmark.filledInvocation
              << "\nAvailable processors       : " << engine.get_numa_config()
              << "\nThread count               : " << benchmark.threads
              << "\nThread binding             : " << threadBinding
              << "\nTT size [MiB]              : " << benchmark.ttSize
              << "\nHash max, avg [per mille]  : "  //
              << "\n    Single search          : " << maxHashFull[0] << ", "
              << sumHashFull[0] / hashFullCount  //
              << "\n    Single game            : " << maxHashFull[1] << ", "
              << sumHashFull[1] / hashFullCount  //
              << "\nTotal time [s]             : " << elapsedTime / 1000.0
              << "\nTotal nodes                : " << nodes
              << "\nnodes/second               : " << 1000 * nodes / elapsedTime << std::endl;

    infoStringStop = false;
    init_update_listeners();
}

namespace {

struct WinRateParams final {
    double a, b;
};

WinRateParams win_rate_params(const Position& pos) noexcept {

    // clang-format off
    constexpr double as[4]{-37.45051876,  121.19101539, -132.78783573, 420.70576692};
    constexpr double bs[4]{ 90.26261072, -137.26549898,   71.10130540,  51.35259597};
    // clang-format on

    // The fitted model only uses data for material counts in [17, 78], and is anchored at count 58 (17.2414e-3).
    double m = 17.2414e-3 * std::clamp(pos.std_material(), 17, 78);
    // Return a = p_a(material) and b = p_b(material).
    double a = m * (m * (m * as[0] + as[1]) + as[2]) + as[3];
    double b = m * (m * (m * bs[0] + bs[1]) + bs[2]) + bs[3];

    return {a, b};
}

// The win rate model is 1 / (1 + exp((a - eval) / b)), where a = p_a(material) and b = p_b(material).
// It fits the LTC fishtest statistics rather accurately.
int win_rate_model(Value v, const Position& pos) noexcept {
    assert(is_ok(v));
    auto [a, b] = win_rate_params(pos);

    // Return the win rate in per mille units, rounded to the nearest integer.
    return int(0.5 + 1000.0 / (1.0 + std::exp((a - v) / b)));
}
}  // namespace

// Turns a Value to an integer centipawn number,
// without treatment of mate and similar special scores.
int UCI::to_cp(Value v, const Position& pos) noexcept {
    assert(is_ok(v));
    // In general, the score can be defined via the WDL as
    // (log(1/L - 1) - log(1/W - 1)) / (log(1/L - 1) + log(1/W - 1)).
    // Based on our win_rate_model, this simply yields v / a.

    auto [a, b] = win_rate_params(pos);

    return std::round(100 * v / a);
}

std::string UCI::to_wdl(Value v, const Position& pos) noexcept {
    assert(is_ok(v));
    std::ostringstream oss;

    auto wdlW = win_rate_model(+v, pos);
    auto wdlL = win_rate_model(-v, pos);
    auto wdlD = 1000 - wdlW - wdlL;
    oss << wdlW << " " << wdlD << " " << wdlL;

    return oss.str();
}

std::string UCI::format_score(const Score& score) noexcept {
    constexpr int TB_CP = 20000;

    const auto format =
      Overload{[](Score::Unit unit) -> std::string {  //
                   return "cp " + std::to_string(unit.value);
               },
               [](Score::Tablebase tb) -> std::string {
                   return "cp " + std::to_string((tb.win ? +TB_CP : -TB_CP) - tb.ply);
               },
               [](Score::Mate mate) -> std::string {
                   return "mate " + std::to_string(((mate.ply > 0) + mate.ply) / 2);
               }};

    return score.visit(format);
}

char UCI::piece(PieceType pt) noexcept { return is_ok(pt) ? PieceChar[pt] : ' '; }
char UCI::piece(Piece pc) noexcept { return is_ok(pc) ? PieceChar[pc] : ' '; }

Piece UCI::piece(char pc) noexcept {
    auto pos = PieceChar.find(pc);
    return pos != std::string_view::npos ? Piece(pos) : NO_PIECE;
}

std::string UCI::piece_figure(Piece pc) noexcept { return is_ok(pc) ? PieceFigure[pc] : " "; }

char UCI::file(File f, bool upper) noexcept { return int(f) + 'a' - 0x20 * upper; }

char UCI::rank(Rank r) noexcept { return int(r) + '1'; }

std::string UCI::square(Square s) noexcept {
    assert(is_ok(s));
    return std::string{file(file_of(s)), rank(rank_of(s))};
}

std::string UCI::move_to_can(const Move& m) noexcept {
    if (m == Move::None())
        return "(none)";
    if (m == Move::Null())
        return "0000";

    Square org = m.org_sq(), dst = m.dst_sq();
    if (m.type_of() == CASTLING && !Position::Chess960)
    {
        assert(rank_of(org) == rank_of(dst));
        dst = make_square(org < dst ? FILE_G : FILE_C, rank_of(org));
    }

    std::string can = square(org) + square(dst);
    if (m.type_of() == PROMOTION)
        can += char(std::tolower(piece(m.promotion_type())));

    return can;
}

// Converts a string representing a move in coordinate notation
// (g1f3, a7a8q) to the corresponding legal move, if any.
Move UCI::can_to_move(std::string can, const LegalMoveList& legalMoves) noexcept {
    assert(4 <= can.size() && can.size() <= 5);
    can = lower_case(can);

    for (const Move& m : legalMoves)
        if (can == move_to_can(m))
            return m;

    return Move::None();
}

Move UCI::can_to_move(const std::string& can, const Position& pos) noexcept {
    return can_to_move(can, LegalMoveList(pos));
}

namespace {

void on_update_end(const EndInfo& info) noexcept {
    std::cout << "info"
              << " depth " << "0" << " score " << (info.inCheck ? "mate " : "cp ") << "0"
              << std::endl;
}

void on_update_full(const FullInfo& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                                   //
        << " depth " << info.depth                                  //
        << " seldepth " << info.rootMove.selDepth                   //
        << " multipv " << info.multiPV                              //
        << " score " << UCI::format_score({info.value, info.pos});  //
    if (info.boundShow)
        oss << (info.rootMove.boundLower   ? " lowerbound"
                : info.rootMove.boundUpper ? " upperbound"
                                           : "");
    if (info.wdlShow)
        oss << " wdl " << UCI::to_wdl(info.value, info.pos);
    oss << " time " << info.time                     //
        << " nodes " << info.nodes                   //
        << " nps " << 1000 * info.nodes / info.time  //
        << " hashfull " << info.hashFull             //
        << " tbhits " << info.tbHits                 //
        << " pv";
    for (const Move& m : info.rootMove)
        oss << ' ' << UCI::move_to_can(m);
    std::cout << oss.str() << std::endl;
}

void on_update_iter(const IterInfo& info) noexcept {
    std::ostringstream oss;
    oss << "info"                                           //
        << " depth " << info.depth                          //
        << " currmove " << UCI::move_to_can(info.currMove)  //
        << " currmovenumber " << info.currMoveNumber;       //
    std::cout << oss.str() << std::endl;
}

void on_update_move(const MoveInfo& info) noexcept {
    std::cout << "bestmove " << UCI::move_to_can(info.bestMove);
    if (info.ponderMove != Move::None())
        std::cout << " ponder " << UCI::move_to_can(info.ponderMove);
    std::cout << std::endl;
}

enum Ambiguity : std::uint8_t {
    AMB_NONE,
    AMB_RANK,
    AMB_FILE,
    AMB_SQUARE,
};

// Ambiguity if more then one piece of same type can reach 'to' with a legal move.
// NOTE: for pawns it is not needed because 'org' file is explicit.
Ambiguity ambiguity(const Move& m, const Position& pos) noexcept {
    assert(pos.pseudo_legal(m) && pos.legal(m));

    Color ac = pos.active_color();

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(pos.piece_on(org)) == ac);
    PieceType pt = type_of(pos.piece_on(org));

    // If there is only one piece then move cannot be ambiguous
    if (pos.count(ac, pt) == 1)
        return AMB_NONE;

    // Disambiguation if have more then one piece with destination
    // note that for pawns is not needed because starting file is explicit.
    Bitboard piece = (attacks_bb(pt, dst, pos.pieces()) & pos.pieces(ac, pt)) ^ org;

    if (!piece)
        return AMB_NONE;

    Bitboard b = piece;
    // If pinned piece is considered as ambiguous
    //& ~pos.blockers(ac);
    while (b)
    {
        Square sq = pop_lsb(b);

        Move mm = Move(sq, dst);
        if (!(pos.pseudo_legal(mm) && pos.legal(mm)))
            piece ^= sq;
    }
    if (!(piece & file_bb(org)))
        return AMB_RANK;
    if (!(piece & rank_bb(org)))
        return AMB_FILE;

    return AMB_SQUARE;
}

}  // namespace

std::string UCI::move_to_san(const Move& m, Position& pos) noexcept {
    if (m == Move::None())
        return "(none)";
    if (m == Move::Null())
        return "0000";
    assert(LegalMoveList(pos).contains(m));

    Square org = m.org_sq(), dst = m.dst_sq();
    assert(color_of(pos.piece_on(org)) == pos.active_color());

    auto pt = type_of(pos.piece_on(org));

    std::string san;

    if (m.type_of() == CASTLING)
    {
        assert(pt == KING && rank_of(org) == rank_of(dst));
        san = (org < dst ? "O-O" : "O-O-O");
    }
    else
    {
        if (pt != PAWN)
        {
            san = piece(pt);
            if (pt != KING)
            {
                // Disambiguation if have more then one piece of type 'pt' that can reach 'to' with a legal move.
                switch (ambiguity(m, pos))
                {
                case AMB_RANK :
                    san += file(file_of(org));
                    break;
                case AMB_FILE :
                    san += rank(rank_of(org));
                    break;
                case AMB_SQUARE :
                    san += square(org);
                    break;
                default :;
                }
            }
        }

        if (pos.capture(m))
        {
            if (pt == PAWN)
                san = file(file_of(org));
            san += 'x';
        }

        san += square(dst);

        if (m.type_of() == PROMOTION)
        {
            assert(pt == PAWN);
            san += std::string{'=', piece(m.promotion_type())};
        }
    }

    State st;
    ASSERT_ALIGNED(&st, CACHE_LINE_SIZE);

    bool check = pos.check(m);
    pos.do_move(m, st, check);

    // Marker for check & checkmate
    if (check)
        san += (LegalMoveList(pos).empty() ? '#' : '+');
    else if (LegalMoveList(pos).empty())
        san += '=';

    pos.undo_move(m);

    return san;
}

Move UCI::san_to_move(std::string san, Position& pos, const LegalMoveList& legalMoves) noexcept {
    assert(2 <= san.size() && san.size() <= 9);
    if (starts_with(lower_case(san), "o-") || starts_with(san, "0-"))
        for (char ch : {'o', '0'})
            std::replace(san.begin(), san.end(), ch, 'O');

    for (const Move& m : legalMoves)
        if (san == move_to_san(m, pos))
            return m;

    return Move::None();
}

Move UCI::san_to_move(const std::string& san, Position& pos) noexcept {
    return san_to_move(san, pos, LegalMoveList(pos));
}

Move UCI::mix_to_move(const std::string&   mix,
                      Position&            pos,
                      const LegalMoveList& legalMoves) noexcept {
    assert(2 <= mix.size() && mix.size() <= 9);
    Move m = Move::None();

    if (mix.size() >= 2)
    {
        if (mix.size() < 4 || starts_with(lower_case(mix), "o-") || starts_with(mix, "0-"))
            return san_to_move(mix, pos, legalMoves);
        if (mix.size() <= 5)
            m = can_to_move(mix, legalMoves);
        if (m == Move::None() && mix.size() <= 9)
            m = san_to_move(mix, pos, legalMoves);
    }
    return m;
}

}  // namespace DON
