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

#include "engine.h"

#include <atomic>
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include "evaluate.h"
#include "misc.h"
#include "movegen.h"
#include "perft.h"
#include "uci.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace {

constexpr inline std::size_t MIN_THREADS = 1u;
constexpr inline std::size_t MAX_THREADS = 1024u;

constexpr inline std::size_t MIN_HASH = 4u;
constexpr inline std::size_t MAX_HASH =
#if defined(IS_64BIT)
  0x2000000u
#else
  0x800u
#endif
  ;

}  // namespace

Engine::Engine(std::optional<std::string> path) noexcept :
    // clang-format off
    binaryDirectory(path ? CommandLine::get_binary_directory(*path) : ""),
    numaContext(NumaConfig{}),
    options(),
    threads(),
    tt(),
    networks(
      numaContext,
      NNUE::Networks(NNUE::BigNetwork  ({EvalFileDefaultNameBig  , "None", ""}, NNUE::BIG),
                     NNUE::SmallNetwork({EvalFileDefaultNameSmall, "None", ""}, NNUE::SMALL))) {
    
    options.add("NumaPolicy",       Option("none", "var none var auto var system var hardware var default", [this](const Option& o) {
        set_numa_config(o);
        return get_numa_config_info_str() + '\n'  //
             + get_thread_allocation_info_str();
    }));
    options.add("Threads",          Option(MIN_THREADS, MIN_THREADS, MAX_THREADS, [this](const Option&) {
        resize_threads_tt();
        return get_thread_allocation_info_str();
    }));
    options.add("Hash",                 Option(16, MIN_HASH, MAX_HASH, [this](const Option& o) {
        resize_tt(o);
        return "Hash: " + std::to_string(int(o));
    }));
    options.add("Clear Hash",           Option([this](const Option&) { init(); return std::nullopt; }));
    options.add("HashRetain",           Option(false));
    options.add("HashFile",             Option(""));
    options.add("Save Hash",            Option([this](const Option& o) { tt.save(o);          return std::nullopt; }));
    options.add("Load Hash",            Option([this](const Option& o) { tt.load(o, threads); return std::nullopt; }));
    options.add("Ponder",               Option(false));
    options.add("MultiPV",              Option(MinMultiPV, 1, 256));
    options.add("SkillLevel",           Option(int(Skill::MaxLevel), int(Skill::MinLevel), int(Skill::MaxLevel)));
    options.add("MoveOverhead",         Option(10, 0, 5000));
    options.add("NodesTime",            Option(0, 0, 10000));
    options.add("DrawMoveCount",        Option(DrawMoveCount, 5, DrawMoveCount, [](const Option& o) { DrawMoveCount = int(o); return std::nullopt; }));
    options.add("UCI_Chess960",         Option(Chess960, [](const Option& o) { Chess960 = bool(o); return std::nullopt; }));
    options.add("UCI_LimitStrength",    Option(false));
    options.add("UCI_ELO",              Option(Skill::MaxELO, Skill::MinELO, Skill::MaxELO));
    options.add("UCI_ShowWDL",          Option(false));
    options.add("UCI_ShowCurrLine",     Option(false));
    options.add("UCI_ShowRefutations",  Option(false));
    options.add("OwnBook",              Option(false));
    options.add("BookFile",             Option("", [](const Option& o) { Search::load_book(o); return std::nullopt; }));
    options.add("BookDepth",            Option(100, 1, 256));
    options.add("BookBestPick",         Option(true));
    options.add("SyzygyPath",           Option("", [](const Option& o) { Tablebases::init(o); return std::nullopt; }));
    options.add("SyzygyProbeLimit",     Option(7, 0, 7));
    options.add("SyzygyProbeDepth",     Option(1, 1, 100));
    options.add("Syzygy50MoveRule",     Option(true));
    options.add("EvalFileBig",          Option(EvalFileDefaultNameBig  , [this](const Option& o) { load_big_network(o);   return std::nullopt; }));
    options.add("EvalFileSmall",        Option(EvalFileDefaultNameSmall, [this](const Option& o) { load_small_network(o); return std::nullopt; }));
    options.add("ReportMinimal",        Option(false));
    options.add("DebugLogFile",         Option("", [](const Option& o) { start_logger(o); return std::nullopt; }));
    // clang-format on

    load_networks();
    resize_threads_tt();

    setup(UCI::StartFEN);
}

Engine::~Engine() noexcept { wait_finish(); }

const Options& Engine::get_options() const noexcept { return options; }
Options&       Engine::get_options() noexcept { return options; }

std::string Engine::fen() const noexcept { return pos.fen(); }

void Engine::setup(std::string_view fen, const std::vector<std::string>& moves) noexcept {
    // Drop the old states and create a new one
    states = std::make_unique<StateList>(1);
    pos.set(fen, &states->back());

    for (const auto& move : moves)
    {
        const LegalMoveList legalMoves(pos);
        if (legalMoves.empty())
            break;
        Move m = UCI::mix_to_move(move, pos, legalMoves);
        if (m == Move::None)
        {
            assert(false);
            break;
        }
        assert(pos.rule50_count() <= 100);
        states->emplace_back();
        pos.do_move(m, states->back());
    }
}

std::uint64_t Engine::perft(Depth depth, bool detail) noexcept {
    return Benchmark::perft(pos, depth, options["Hash"], threads, detail);
}

void Engine::start(const Limit& limit) noexcept {
    assert(!limit.perft);

    verify_networks();
    threads.start(pos, states, limit, options);
}

void Engine::stop() noexcept { threads.stop = true; }

void Engine::ponderhit() noexcept { threads.main_manager()->ponder = false; }

void Engine::wait_finish() const noexcept { threads.main_thread()->wait_finish(); }

void Engine::init() noexcept {
    if (options["HashRetain"])
        return;

    wait_finish();
    threads.init();
    tt.init(threads);
    // @TODO won't work with multiple instances
    Tablebases::init(options["SyzygyPath"]);  // Free mapped files
}

void Engine::resize_threads_tt() noexcept {
    threads.set(numaContext.get_numa_config(), {options, networks, threads, tt}, updateContext);
    // Reallocate the hash with the new threadpool size
    resize_tt(options["Hash"]);
    threads.ensure_network_replicated();
}

void Engine::resize_tt(std::size_t ttSize) noexcept {
    wait_finish();
    tt.resize(ttSize, threads);
}

void Engine::show() const noexcept { std::cout << pos << std::endl; }

void Engine::eval() noexcept {
    verify_networks();
    std::cout << '\n' << trace(pos, *networks) << std::endl;
}

void Engine::flip() noexcept { pos.flip(); }

std::uint16_t Engine::get_hashFull(std::uint8_t maxAge) const noexcept {
    return tt.hashFull(maxAge);
}

void Engine::set_numa_config(const std::string& str) noexcept {
    if (str == "none")
        numaContext.set_numa_config(NumaConfig{});

    else if (str == "auto" || str == "system")
        numaContext.set_numa_config(NumaConfig::from_system(true));

    else if (str == "hardware")
        // Don't respect affinity set in the system.
        numaContext.set_numa_config(NumaConfig::from_system(false));

    else if (str == "default")
        numaContext.set_numa_config(
          NumaConfig::from_string("0-15,128-143:16-31,144-159:32-47,160-175:48-63,176-191"));

    else
        numaContext.set_numa_config(NumaConfig::from_string(str));

    // Force reallocation of threads in case affinities need to change.
    resize_threads_tt();
}

std::string Engine::get_numa_config_str() const noexcept {
    return numaContext.get_numa_config().to_string();
}

std::string Engine::get_numa_config_info_str() const noexcept {
    return "Available Processors: " + get_numa_config_str();
}

std::vector<std::pair<std::size_t, std::size_t>> Engine::get_bound_thread_counts() const noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> ratios;

    auto              counts = threads.get_bound_thread_counts();
    const NumaConfig& config = numaContext.get_numa_config();

    NumaIndex numaIdx = 0;
    for (; numaIdx < counts.size(); ++numaIdx)
        ratios.emplace_back(counts[numaIdx], config.num_cpus_in_numa_node(numaIdx));
    if (!counts.empty())
        for (; numaIdx < config.num_numa_nodes(); ++numaIdx)
            ratios.emplace_back(0, config.num_cpus_in_numa_node(numaIdx));
    return ratios;
}

std::string Engine::get_thread_binding_info_str() const noexcept {
    std::ostringstream oss;

    auto boundThreadCounts = get_bound_thread_counts();
    if (!boundThreadCounts.empty())
    {
        bool first = true;
        for (auto&& [count, total] : boundThreadCounts)
        {
            if (!first)
                oss << ':';
            oss << count << '/' << total;
            first = false;
        }
    }

    return oss.str();
}

std::string Engine::get_thread_allocation_info_str() const noexcept {
    std::ostringstream oss;

    oss << "Threads: " << threads.size();

    auto boundThreadInfoStr = get_thread_binding_info_str();
    if (!boundThreadInfoStr.empty())
        oss << " with NUMA node thread binding: " << boundThreadInfoStr;

    return oss.str();
}

void Engine::verify_networks() const noexcept {
    networks->big.verify(options["EvalFileBig"]);
    networks->small.verify(options["EvalFileSmall"]);
}

void Engine::load_networks() noexcept {
    networks.modify_and_replicate(  //
      [this](NNUE::Networks& net) {
          net.big.load(binaryDirectory, options["EvalFileBig"]);
          net.small.load(binaryDirectory, options["EvalFileSmall"]);
      });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::load_big_network(const std::string& netFile) noexcept {
    networks.modify_and_replicate(
      [this, &netFile](NNUE::Networks& net) { net.big.load(binaryDirectory, netFile); });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::load_small_network(const std::string& netFile) noexcept {
    networks.modify_and_replicate(
      [this, &netFile](NNUE::Networks& net) { net.small.load(binaryDirectory, netFile); });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::save_networks(const std::array<std::optional<std::string>, 2>& netFiles) noexcept {
    networks.modify_and_replicate(  //
      [&](const NNUE::Networks& net) {
          net.big.save(netFiles[0]);
          net.small.save(netFiles[1]);
      });
}

void Engine::set_on_update_end(OnUpdateEnd&& f) noexcept {
    updateContext.onUpdateEnd = std::move(f);
}

void Engine::set_on_update_full(OnUpdateFull&& f) noexcept {
    updateContext.onUpdateFull = std::move(f);
}

void Engine::set_on_update_iter(OnUpdateIter&& f) noexcept {
    updateContext.onUpdateIter = std::move(f);
}

void Engine::set_on_update_move(OnUpdateMove&& f) noexcept {
    updateContext.onUpdateMove = std::move(f);
}

}  // namespace DON
