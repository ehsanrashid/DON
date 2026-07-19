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
#include <cctype>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>  // IWYU pragma: keep

#include "benchmark.h"
#include "memory.h"
#include "misc.h"
#include "notation.h"
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
  {"stop",       Command::STOP},
  {"quit",       Command::QUIT},
  {"ponderhit",  Command::PONDERHIT},
  {"position",   Command::POSITION},
  {"go",         Command::GO},
  {"setoption",  Command::SETOPTION},
  {"uci",        Command::UCI},
  {"ucinewgame", Command::UCINEWGAME},
  {"isready",    Command::ISREADY},
  {"bench",      Command::BENCH},
  {"benchmark",  Command::BENCHMARK},
  {"show",       Command::SHOW},
  {"dump",       Command::DUMP},
  {"eval",       Command::EVAL},
  {"flip",       Command::FLIP},
  {"mirror",     Command::MIRROR},
  {"compiler",   Command::COMPILER},
  {"export_net", Command::EXPORT_NET},
  {"--help",     Command::HELP},
  {"help",       Command::HELP},
  {"--license",  Command::HELP},
  {"license",    Command::HELP}
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

    std::string token;
    while (is >> token)
    {
        token = lower_case(token);

        if (token == "wtime")
        {
            is >> limit.clocks[WHITE].time;

            limit.clocks[WHITE].time =
              std::max(constexpr_abs(limit.clocks[WHITE].time), TimePoint{1});
        }
        else if (token == "btime")
        {
            is >> limit.clocks[BLACK].time;

            limit.clocks[BLACK].time =
              std::max(constexpr_abs(limit.clocks[BLACK].time), TimePoint{1});
        }
        else if (token == "winc")
        {
            is >> limit.clocks[WHITE].inc;

            limit.clocks[WHITE].inc =
              std::max(constexpr_abs(limit.clocks[WHITE].inc), TimePoint{1});
        }
        else if (token == "binc")
        {
            is >> limit.clocks[BLACK].inc;

            limit.clocks[BLACK].inc =
              std::max(constexpr_abs(limit.clocks[BLACK].inc), TimePoint{1});
        }
        else if (token == "movetime")
        {
            is >> limit.moveTime;

            limit.moveTime = std::max(constexpr_abs(limit.moveTime), TimePoint{1});
        }
        else if (token == "movestogo")
        {
            i16 movesToGo;
            is >> movesToGo;

            limit.movesToGo = std::clamp<u8>(constexpr_abs(movesToGo), 1, 255);
        }
        else if (token == "mate")
        {
            i16 mate;
            is >> mate;

            limit.mate = std::clamp<u8>(constexpr_abs(mate), 1, 255);
        }
        else if (token == "depth")
        {
            is >> limit.depth;

            limit.depth = std::clamp<Depth>(constexpr_abs(limit.depth), 1, DEPTH_MAX);
        }
        else if (token == "nodes")
        {
            is >> limit.nodes;

            limit.nodes = std::max(limit.nodes, u64(1));
        }
        else if (token == "infinite")
            limit.infinite = true;
        else if (token == "ponder")
            limit.ponder = true;
        else if (token == "perft")
        {
            limit.perft = true;
            is >> limit.depth;
            is >> std::boolalpha >> limit.detail;

            limit.depth = std::clamp<Depth>(constexpr_abs(limit.depth), 1, DEPTH_MAX);
        }
        // "searchmoves" needs to be the last command on the line
        else if (!token.empty() && token[0] == 's')  // "searchmoves"
        {
            auto pos = is.tellg();
            while (is >> token
                   && !(!token.empty() && char(std::tolower((unsigned char) token[0])) == 'i'))
            {
                limit.searchMoves.push_back(token);
                pos = is.tellg();
            }
            is.seekg(pos);
        }
        // "ignoremoves" needs to be the last command on the line
        else if (!token.empty() && token[0] == 'i')  // "ignoremoves"
        {
            auto pos = is.tellg();
            while (is >> token
                   && !(!token.empty() && char(std::tolower((unsigned char) token[0])) == 's'))
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


UCI::UCI(const std::filesystem::path& path) noexcept :
    engine(path) {

    options().set_info_callback([](std::optional<std::string_view> infoSv) noexcept {
        if (infoSv)
            print_info_string(*infoSv);
    });

    set_update_callbacks();
}

Options& UCI::options() noexcept { return engine.get_options(); }

void UCI::process_input(std::istream& is) noexcept {

    std::string command;
    command.reserve(1 * _KB);
    do
    {
        // Wait for an input or an end-of-file (EOF) indication
        if (!std::getline(is, command))
            command = "quit";

        execute(command);

    } while (command != "quit");
}

void UCI::execute(std::string_view command) noexcept {

    StringViewStreamBuf buf{command};

    std::istream is{&buf};

    is >> std::skipws;

    std::string token;
    is >> token;

    if (token.empty())
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
        engine.init();
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
        engine.show();
        break;
    case Command::DUMP : {
        std::string      input;
        std::string_view dumpFile;

        if (is >> input)
            dumpFile = input;

        engine.dump(dumpFile);
    }
    break;
    case Command::EVAL :
        engine.eval();
        break;
    case Command::FLIP :
        engine.flip();
        break;
    case Command::MIRROR :
        engine.mirror();
        break;
    case Command::COMPILER :
        std::cout << compiler_info() << std::endl;
        break;
    case Command::EXPORT_NET : {
        Array<std::string, 2>      inputs;
        Array<std::string_view, 2> netFiles;

        for (usize i = 0; i < netFiles.size() && is >> inputs[i]; ++i)
            netFiles[i] = inputs[i];

        engine.save_networks(netFiles);
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
    std::cout << "bestmove " << mInfo.bestMove << " ponder " << mInfo.ponderMove << std::endl;
}

}  // namespace

void UCI::set_update_callbacks() noexcept {
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
    if (token.empty() || char(std::tolower((unsigned char) token[0])) == 's')  // "startpos"
    {
        token.clear();
        fen.assign(START_FEN);
        is >> token;  // Consume the "moves" token, if any
    }
    else if (!token.empty() && char(std::tolower((unsigned char) token[0])) == 'f')  // "fen"
    {
        token.clear();
        fen.reserve(64);

        usize i = 0;
        // Read up to 6 tokens
        while (is >> token && i < 6)
        {
            // Stop if reach "moves" token after the first two fields
            if (i > 1 && !token.empty() && char(std::tolower((unsigned char) token[0])) == 'm')
                break;

            fen.append(token).push_back(' ');
            token.clear();
            ++i;
        }
        // Fill missing fields with "-"
        while (i < 4)
        {
            fen.append("- ");
            ++i;
        }
    }
    else
    {
        assert(false && "Invalid position command");
        return;
    }

    assert(token.empty() || char(std::tolower((unsigned char) token[0])) == 'm');

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
            name.push_back(' ');

        name.append(token);
    }

    // Read the option value (can contain spaces)
    std::string value;
    while (is >> token)
    {
        if (!value.empty())
            value.push_back(' ');

        value.append(token);
    }

    options().set(name, value);
}

void UCI::bench(std::istream& is) noexcept {

    auto commands = Benchmark::bench(is, engine.fen());

    u64 infoNodes = 0;
    engine.set_on_update_full([&infoNodes](const auto& info) {
        infoNodes = info.nodes;
        on_update_full(info);
    });

    auto minimalInfo = bool_to_string(options()["MinimalInfo"]);

    options().set("MinimalInfo", bool_to_string(true));

    usize num = std::count_if(commands.begin(), commands.end(), [](std::string_view command) {
        return starts_with(command, "go ") || starts_with(command, "eval");
    });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    usize cnt   = 0;
    u64   nodes = 0;

    for (const auto& command : commands)
    {
        std::istringstream iss{command};
        iss >> std::skipws;

        std::string token;
        iss >> token;

        if (token.empty())
            continue;

        switch (to_command(lower_case(token)))
        {
        case Command::GO : {
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
        case Command::EVAL :
            std::cerr << "\nPosition: " << ++cnt << '/' << num << " (" << engine.fen() << ")"
                      << std::endl;
            engine.eval();
            break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::SETOPTION :
            setoption(iss);
            break;
        case Command::UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }
    }

    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime + now() - startTime, TimePoint{1});

#if !defined(NDEBUG)
    Debug::print();
#endif

    std::cerr << "\n================"                   //
              << "\nTotal time [ms] : " << elapsedTime  //
              << "\nTotal nodes     : " << nodes        //
              << "\nnodes/second    : " << 1000 * nodes / elapsedTime << std::endl;

    options().set("MinimalInfo", minimalInfo);
    // Reset callback, to not capture a dangling reference to infoNodes
    engine.set_on_update_full(on_update_full);
}

void UCI::benchmark(std::istream& is) noexcept {
    // Probably not very important for a test this long, but include for completeness and sanity.
    constexpr usize WarmupPositionCount = 3;

    auto setup = Benchmark::benchmark(is);

    // Set options once at the start
    options().set("Threads", std::to_string(setup.threads));
    options().set("Hash", std::to_string(setup.ttSize));
    options().set("UCI_Chess960", bool_to_string(false));

    u64 infoNodes = 0;
    engine.set_on_update_short([](const auto&) {});
    engine.set_on_update_full([&](const auto& info) { infoNodes = info.nodes; });
    engine.set_on_update_iter([](const auto&) {});
    engine.set_on_update_move([](const auto&) {});

    InfoStrStop = true;

    usize num = std::count_if(setup.commands.begin(), setup.commands.end(),
                              [](std::string_view command) { return starts_with(command, "go "); });

#if !defined(NDEBUG)
    Debug::clear();
#endif

    TimePoint startTime   = now();
    TimePoint elapsedTime = 0;

    usize cnt   = 0;
    u64   nodes = 0;
    // Warmup
    for (const auto& command : setup.commands)
    {
        std::istringstream iss{command};
        iss >> std::skipws;

        std::string token;
        iss >> token;

        if (token.empty())
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

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::UCINEWGAME :
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
    constexpr Array<u8, 2> HashfullAges{0, 31};

    static_assert(HashfullAges.size() == 2 && HashfullAges[0] == 0 && HashfullAges[1] == 31,
                  "Incorrect HashfullAges[].");

    u16                             hashfullCount = 0;
    Array<u16, HashfullAges.size()> maxHashfull{};
    Array<u32, HashfullAges.size()> sumHashfull{};

    auto update_hashfull = [&]() noexcept -> void {
        ++hashfullCount;
        for (usize i = 0; i < HashfullAges.size(); ++i)
        {
            auto hashfull = engine.hashfull(HashfullAges[i]);

            maxHashfull[i] = std::max(hashfull, maxHashfull[i]);
            sumHashfull[i] += hashfull;
        }
    };

    auto avg = [&hashfullCount](u32 x) noexcept { return double(x) / hashfullCount; };

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

            nodes += infoNodes;
            infoNodes = 0;
        }
        break;
        case Command::POSITION :
            position(iss);
            break;
        case Command::UCINEWGAME :
            elapsedTime += now() - startTime;
            engine.init();  // May take a while
            startTime = now();
            break;
        default :;
        }
    }

    // Ensure non-zero to avoid a 'divide by zero'
    elapsedTime = std::max(elapsedTime + now() - startTime, TimePoint{1});

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
              << "\nTotal time [s]             : " << double(elapsedTime) / 1000.0
              << "\nTotal nodes                : " << nodes
              << "\nnodes/second               : " << 1000 * nodes / elapsedTime << std::endl;
    // clang-format on

    InfoStrStop = false;
    set_update_callbacks();
}

u64 UCI::perft(Depth depth, bool detail) noexcept {
    u64 nodes = engine.perft(depth, detail);

    std::cout << "\nTotal nodes: " << nodes << '\n' << std::endl;

    return nodes;
}

}  // namespace DON
