/*
  DON, UCI chess playing engine Copyright (C) 2003-2026

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
#include <chrono>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "benchmark.h"
#include "memory.h"
#include "misc.h"
#include "option.h"
#include "search.h"

namespace DON {

namespace {

enum class Command : u8 {
    STOP,
    QUIT,
    PONDERHIT,
    POSITION,
    GO,
    SETOPTION,
    UCI,
    UCINEWGAME,
    ISREADY,
    // Add custom non-UCI commands, mainly for debugging purposes.
    // These commands must not be used during a search!
    BENCH,
    BENCHMARK,
    SHOW,
    DUMP,
    EVAL,
    FLIP,
    MIRROR,
    COMPILER,
    EXPORT_NET,
    HELP,
    // Unknown Command
    NONE,
};

// clang-format off
const std::unordered_map<std::string_view, Command> COMMANDS{
  {"stop"      , Command::STOP},
  {QUIT_CMD    , Command::QUIT},
  {"ponderhit" , Command::PONDERHIT},
  {"position"  , Command::POSITION},
  {"go"        , Command::GO},
  {"setoption" , Command::SETOPTION},
  {"uci"       , Command::UCI},
  {"ucinewgame", Command::UCINEWGAME},
  {"isready"   , Command::ISREADY},
  {"bench"     , Command::BENCH},
  {"benchmark" , Command::BENCHMARK},
  {"show"      , Command::SHOW},
  {"dump"      , Command::DUMP},
  {"eval"      , Command::EVAL},
  {"flip"      , Command::FLIP},
  {"mirror"    , Command::MIRROR},
  {"compiler"  , Command::COMPILER},
  {"export_net", Command::EXPORT_NET},
  {"--help"    , Command::HELP},
  {"help"      , Command::HELP},
  {"--license" , Command::HELP},
  {"license"   , Command::HELP}
};
// clang-format on

Command to_command(std::string_view command) noexcept {
    auto itr = COMMANDS.find(command);

    return itr != COMMANDS.end() ? itr->second : Command::NONE;
}

Limit parse_limit(std::istream& is) noexcept {

    Limit limit{};
    // The search starts as early as possible
    limit.startTime = now();

    bool        tokenReady = false;
    std::string token;
    while (tokenReady || is >> token)
    {
        tokenReady = false;
        token      = lower_case(token);

        if (token == "wtime")
        {
            is >> limit.clocks[WHITE].time;

            limit.clocks[WHITE].time =
              std::max<TimePoint>(constexpr_abs(limit.clocks[WHITE].time), 1);
        }
        else if (token == "btime")
        {
            is >> limit.clocks[BLACK].time;

            limit.clocks[BLACK].time =
              std::max<TimePoint>(constexpr_abs(limit.clocks[BLACK].time), 1);
        }
        else if (token == "winc")
        {
            is >> limit.clocks[WHITE].inc;

            limit.clocks[WHITE].inc =
              std::max<TimePoint>(constexpr_abs(limit.clocks[WHITE].inc), 1);
        }
        else if (token == "binc")
        {
            is >> limit.clocks[BLACK].inc;

            limit.clocks[BLACK].inc =
              std::max<TimePoint>(constexpr_abs(limit.clocks[BLACK].inc), 1);
        }
        else if (token == "movetime")
        {
            is >> limit.moveTime;

            limit.moveTime = std::max<TimePoint>(constexpr_abs(limit.moveTime), 1);
        }
        else if (token == "movestogo")
        {
            i16 movesToGo;
            is >> movesToGo;

            limit.movesToGo = std::clamp<u8>(constexpr_abs(u8(movesToGo)), 1, 255);
        }
        else if (token == "mate")
        {
            i16 mate;
            is >> mate;

            limit.mate = std::clamp<u8>(constexpr_abs(u8(mate)), 1, 255);
        }
        else if (token == "depth")
        {
            is >> limit.depth;

            limit.depth = std::clamp<Depth>(constexpr_abs(limit.depth), 1, DEPTH_MAX);
        }
        else if (token == "nodes")
        {
            is >> limit.nodes;

            limit.nodes = std::max(limit.nodes, u64{1});
        }
        else if (token == "infinite")
            limit.infinite = true;
        else if (token == "ponder")
            limit.ponder = true;
        // "perft" needs to be the last command on the line
        else if (token == "perft")
        {
            limit.perft = true;
            is >> limit.depth;
            is >> std::boolalpha >> limit.detail;

            limit.depth = std::clamp<Depth>(constexpr_abs(limit.depth), 1, DEPTH_MAX);
            break;
        }
        // "searchmoves" needs to be the last command on the line
        else if (token[0] == 's')
        {
            while (is >> token)
            {
                if (lower_case(token[0]) == 'i')
                {
                    tokenReady = true;
                    break;
                }

                limit.searchMoves.push_back(token);
            }

            if (is.eof())
                is.clear();
        }
        // "ignoremoves" needs to be the last command on the line
        else if (token[0] == 'i')
        {
            while (is >> token)
            {
                if (lower_case(token[0]) == 's')
                {
                    tokenReady = true;
                    break;
                }

                limit.ignoreMoves.push_back(token);
            }

            if (is.eof())
                is.clear();
        }

        if (!is)
            terminate_on_critical_error("Invalid argument for '" + token + "'");
    }

    return limit;
}

}  // namespace


UCI::UCI(const fs::path& path) noexcept :
    engine(path) {

    options().set_on_info([](Options::Info info) noexcept {
        if (info)
            print_info_string(*info);
    });

    set_on_updates();
}

Options& UCI::options() noexcept { return engine.options(); }

const Options& UCI::options() const noexcept { return engine.options(); }

void UCI::process_input(std::istream& is) noexcept {

    std::string command;
    command.reserve(1 * KB);

    // Wait for an input or an end-of-file (EOF) indication
    while (std::getline(is, command))
    {
        if (command == QUIT_CMD)
            break;

        execute(command);
    }
}

void UCI::execute(const std::string_view command) noexcept {

    StringViewBuf svBuf{command};

    std::istream is{&svBuf};

    std::string token;
    if (!(is >> token))
        return;

    switch (to_command(lower_case(token)))
    {
    case Command::STOP :
    case Command::QUIT :
        engine.stop();
        break;
    case Command::PONDERHIT :
        // The GUI sends 'ponderhit' to tell that the user has played the expected move.
        // So, 'ponderhit' is sent if pondering was done on the same move that the user has played.
        // The search should continue, but should also switch from pondering to the normal search.
        engine.ponderhit();
        break;
    case Command::POSITION :
        position(is);
        break;
    case Command::GO :
        // Send info strings after the go command is sent for old GUIs and python-chess
        print_info_string(engine.numa_config_info());
        print_info_string(engine.thread_allocation());

        go(is);
        break;
    case Command::SETOPTION :
        setoption(is);
        break;
    case Command::UCI :
        std::cout << engine_info(true) << '\n'  //
                  << options() << '\n'          //
                  << "uciok" << std::endl;
        break;
    case Command::UCINEWGAME :
        engine.reset();
        break;
    case Command::ISREADY :
        std::cout << "readyok" << std::endl;
        break;
    // Add custom non-UCI commands, mainly for debugging purposes.
    // These commands must not be used during a search!
    case Command::BENCH :
        bench(is);
        break;
    case Command::BENCHMARK :
        benchmark(is);
        break;
    case Command::SHOW :
        std::cout << engine.position() << std::endl;
        break;
    case Command::DUMP : {
        std::string input;
        fs::path    dumpFilePath;

        if (is >> input)
            dumpFilePath = input;

        engine.dump(dumpFilePath);
    }
    break;
    case Command::EVAL :
        std::cout << '\n' << engine.evaluation() << std::endl;
        break;
    case Command::FLIP :
        if (auto err = engine.flip())
            terminate_on_critical_error(err->what());
        break;
    case Command::MIRROR :
        if (auto err = engine.mirror())
            terminate_on_critical_error(err->what());
        break;
    case Command::COMPILER :
        std::cout << compiler_info() << std::endl;
        break;
    case Command::EXPORT_NET : {
        std::string input;
        fs::path    netFilePath;

        if (is >> input)
            netFilePath = utf8_to_path(input);

        engine.save_network(netFilePath);
    }
    break;
    case Command::HELP :
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
            std::cout << "Unknown command: '" << command << "'.\n"
                      << "Type help for more information." << std::endl;
    }
}

namespace {

void on_update_short(const ShortInfo& sInfo) noexcept {
    std::cout << "info"                    //
              << " depth " << sInfo.depth  //
              << " score " << sInfo.score << std::endl;
}

void on_update_full(const FullInfo& fInfo) noexcept {
    std::cout << "info"                                      //
              << " depth " << fInfo.depth                    //
              << " seldepth " << fInfo.selDepth              //
              << " multipv " << fInfo.multiPV                //
              << " score " << fInfo.score                    //
              << fInfo.bound                                 //
              << fInfo.wdl                                   //
              << " time " << fInfo.time                      //
              << " nodes " << fInfo.nodes                    //
              << " nps " << 1000 * fInfo.nodes / fInfo.time  //
              << " tbhits " << fInfo.tbHits                  //
              << " hashfull " << fInfo.hashfull              //
              << " pv" << fInfo.pv << std::endl;
}

void on_update_iter(const IterInfo& iInfo) noexcept {
    std::cout << "info"                          //
              << " depth " << iInfo.depth        //
              << " currmove " << iInfo.currMove  //
              << " currmovenumber " << iInfo.currMoveNumber << std::endl;
}

void on_update_move(const MoveInfo& mInfo) noexcept {
    std::cout << "bestmove " << mInfo.bestMove;
    if (!mInfo.ponderMove.empty())
        std::cout << " ponder " << mInfo.ponderMove;
    std::cout << std::endl;
}

}  // namespace

u64 UCI::perft(const Depth depth, const bool detail) const noexcept {
    const u64 nodes = engine.perft(depth, detail);

    std::cout << "\nTotal nodes: " << nodes << '\n' << std::endl;

    return nodes;
}

void UCI::set_on_updates() noexcept {
    engine.set_on_update_start([]() {});
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

    if (token.empty() || lower_case(token[0]) == 's')  // "startpos"
    {
        fen.append(START_FEN);
        token.clear();
        is >> token;  // Consume the "moves" token, if any
    }
    else if (lower_case(token[0]) == 'f')  // "fen"
    {
        fen.reserve(64);

        usize i = 0;
        // Read up to 6 tokens
        for (; is >> token && i < 6; ++i)
        {
            // Stop if reach "moves" token after the first two fields
            if (i > 1 && lower_case(token[0]) == 'm')
                break;

            fen.append(token).push_back(' ');
        }
        // Fill missing fields with "-"
        for (; i < 4; ++i)
        {
            fen.push_back('-');
            fen.push_back(' ');
        }

        if (!token.empty() && lower_case(token[0]) != 'm')
            token.clear();
    }
    else
        terminate_on_critical_error("Invalid position token: " + token);

    assert(token.empty() || lower_case(token[0]) == 'm');

    Strings moves;
    while (is >> token)
        moves.push_back(token);

    if (auto err = engine.setup(fen, moves))
        terminate_on_critical_error(err->what());
}

void UCI::go(std::istream& is) noexcept {
    auto limit = parse_limit(is);

    if (limit.perft)
    {
        perft(limit.depth, limit.detail);
    }
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
        name.append(token).push_back(' ');

    if (!name.empty())
        name.pop_back();

    // Read the option value (can contain spaces)
    std::string value;
    while (is >> token)
        value.append(token).push_back(' ');

    if (!value.empty())
        value.pop_back();

    options().setoption(name, value);
}

void UCI::bench(std::istream& is) noexcept {

    const auto MinimalInfo = bool_to_string(options()["MinimalInfo"]);

    options().setoption("MinimalInfo", bool_to_string(true));

    const auto commands = Benchmark::bench(is, engine.fen());

    const usize num =
      std::count_if(commands.begin(), commands.end(), [](const std::string_view command) {
          return starts_with(command, "go ") || starts_with(command, "eval");
      });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    SteadyClock::time_point startTime;
    SteadyClock::duration   totalDuration{0};

    u64 nodes = 0, totalNodes = 0;

    engine.set_on_update_start([&startTime, &nodes]() noexcept -> void {
        startTime = SteadyClock::now();
        nodes     = 0;
    });
    engine.set_on_update_full([&nodes](const auto& info) noexcept -> void {
        nodes = info.nodes;
        on_update_full(info);
    });
    engine.set_on_update_move([&totalDuration, &totalNodes, &startTime = std::as_const(startTime),
                               &nodes = std::as_const(nodes)](const auto& info) noexcept -> void {
        totalDuration += SteadyClock::now() - startTime;
        totalNodes += nodes;
        on_update_move(info);
    });

    usize cnt = 0;

    for (const auto& command : commands)
    {
        std::istringstream iss{command};

        std::string token;
        if (!(iss >> token))
            continue;

        switch (to_command(lower_case(token)))
        {
        case Command::GO : {
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")"
                      << std::endl;

            auto limit = parse_limit(iss);

            if (limit.perft)
            {
                startTime = SteadyClock::now();
                nodes     = perft(limit.depth, limit.detail);
                totalDuration += SteadyClock::now() - startTime;
                totalNodes += nodes;
            }
            else
            {
                engine.start(limit);
                engine.wait_finish();
            }
        }
        break;
        case Command::EVAL :
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")"
                      << std::endl;
            startTime = SteadyClock::now();
            std::cout << '\n' << engine.evaluation() << std::endl;
            totalDuration += SteadyClock::now() - startTime;
            break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::SETOPTION :
            setoption(iss);
            break;
        case Command::UCINEWGAME :
            engine.reset();  // May take a while
            break;
        default :;
        }
    }

    // Ensure non-zero to avoid a 'divide by zero'
    const auto totalTimeMs =
      std::max(std::chrono::duration_cast<Ms>(totalDuration).count(), TimePoint{1});

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << "\n================"                   //
              << "\nTotal time [ms] : " << totalTimeMs  //
              << "\nTotal nodes     : " << totalNodes   //
              << "\nnodes/second    : " << totalNodes * 1000 / totalTimeMs << std::endl;

    // Reset callback, to not capture a dangling reference
    set_on_updates();
    options().setoption("MinimalInfo", MinimalInfo);
}

void UCI::benchmark(std::istream& is) noexcept {
    // Probably not very important for a test this long, but include for completeness and sanity.
    constexpr usize WarmupPositionCount = 3;

    infoStopped = true;
    engine.set_on_update_short([](const auto&) noexcept -> void {});
    engine.set_on_update_full([&](const auto&) noexcept -> void {});
    engine.set_on_update_iter([](const auto&) noexcept -> void {});
    engine.set_on_update_move([](const auto&) noexcept -> void {});

    const auto setup = Benchmark::benchmark(is);

    // Set options once at the start
    options().setoption("Threads", std::to_string(setup.threads));
    options().setoption("Hash", std::to_string(setup.ttSize));
    options().setoption("UCI_Chess960", bool_to_string(false));

    const usize num =
      std::count_if(setup.commands.begin(), setup.commands.end(),
                    [](const std::string_view command) { return starts_with(command, "go "); });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    usize cnt = 0;
    // Warmup
    for (const auto& command : setup.commands)
    {
        std::istringstream iss{command};

        std::string token;
        if (!(iss >> token))
            continue;

        switch (to_command(lower_case(token)))
        {
        case Command::GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rWarmup position " << ++cnt << '/' << WarmupPositionCount;

            auto limit = parse_limit(iss);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();
        }
        break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::UCINEWGAME :
            engine.reset();  // May take a while
            break;
        default :;
        }

        if (cnt >= WarmupPositionCount)
            break;
    }

    std::cerr << '\n';

    engine.reset();  // May take a while

    // Only normal hashfull and touched hash
    constexpr Array<u8, 2> HashfullAges{0, 31};

    static_assert(HashfullAges.size() == 2 && HashfullAges[0] == 0 && HashfullAges[1] == 31,
                  "Incorrect HashfullAges[].");

    u16                             hashfullCount = 0;
    Array<u16, HashfullAges.size()> maxHashfull{};
    Array<u32, HashfullAges.size()> sumHashfull{};

    const auto update_hashfull = [&]() noexcept -> void {
        ++hashfullCount;
        for (usize i = 0; i < HashfullAges.size(); ++i)
        {
            auto hashfull = engine.hashfull(HashfullAges[i]);

            maxHashfull[i] = std::max(hashfull, maxHashfull[i]);
            sumHashfull[i] += hashfull;
        }
    };

    const auto avg = [&hashfullCount](u32 x) noexcept -> double {
        return double(x) / hashfullCount;
    };

    SteadyClock::time_point startTime;
    SteadyClock::duration   totalDuration{0};

    u64 nodes = 0, totalNodes = 0;

    engine.set_on_update_start([&startTime, &nodes]() noexcept -> void {
        startTime = SteadyClock::now();
        nodes     = 0;
    });
    engine.set_on_update_full([&nodes](const auto& info) noexcept -> void { nodes = info.nodes; });
    engine.set_on_update_move([&totalDuration, &totalNodes, &startTime = std::as_const(startTime),
                               &nodes = std::as_const(nodes)](const auto&) noexcept -> void {
        totalDuration += SteadyClock::now() - startTime;
        totalNodes += nodes;
    });

    cnt = 0;

    for (const auto& command : setup.commands)
    {
        std::istringstream iss{command};

        std::string token;
        if (!(iss >> token))
            continue;

        switch (to_command(lower_case(token)))
        {
        case Command::GO : {
            // One new line is produced by the search, so omit it here
            std::cerr << "\rPosition " << ++cnt << '/' << num;

            auto limit = parse_limit(iss);

            // Run with silenced network verification
            engine.start(limit);
            engine.wait_finish();

            update_hashfull();
        }
        break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::UCINEWGAME :
            engine.reset();  // May take a while
            break;
        default :;
        }
    }

    // Ensure non-zero to avoid a 'divide by zero'
    const auto totalTimeMs =
      std::max(std::chrono::duration_cast<Ms>(totalDuration).count(), TimePoint{1});

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << '\n';

    std::string threadBinding{engine.thread_binding()};
    if (threadBinding.empty())
        threadBinding = "<none>";

    // clang-format off
    std::cerr << "\n==========================="
              << "\nVersion                    : " << version_info()
              << "\nCompiler                   : " << compiler_info()
              << "\nLarge page                 : " << bool_to_string(has_large_page())
              << "\nOriginal invocation        : " << "benchmark " << setup.originalInvocation
              << "\nCurrent invocation         : " << "benchmark " << setup.currentInvocation
              << "\nAvailable processors       : " << engine.numa_config()
              << "\nThread count               : " << setup.threads
              << "\nThread binding             : " << threadBinding
              << "\nTT size [MiB]              : " << setup.ttSize
              << "\nHash max, sum, avg [mille] : Count=" << hashfullCount
              << "\n    Single search          : " << maxHashfull[0] << ", " << sumHashfull[0] << ", " << avg(sumHashfull[0])
              << "\n    Single game            : " << maxHashfull[1] << ", " << sumHashfull[1] << ", " << avg(sumHashfull[1])
              << "\nTotal time [s]             : " << totalTimeMs / 1000.0
              << "\nTotal nodes                : " << totalNodes
              << "\nnodes/second               : " << totalNodes * 1000 / totalTimeMs << std::endl;
    // clang-format on

    set_on_updates();
    infoStopped = false;
}

}  // namespace DON
