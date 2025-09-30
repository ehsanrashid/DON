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

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <iostream>
#include <memory>
#include <sstream>
#include <utility>

#include "evaluate.h"
#include "movegen.h"
#include "numa.h"
#include "perft.h"
#include "polybook.h"
#include "syzygy/tbbase.h"
#include "uci.h"

namespace DON {

namespace {

constexpr std::size_t MIN_THREADS = 1U;
const std::size_t     MAX_THREADS = std::max(4U * hardware_concurrency(), std::size_t(1024));

constexpr std::size_t MIN_HASH = 4U;
constexpr std::size_t MAX_HASH =
#if defined(IS_64BIT)
  0x2000000U
#else
  0x800U
#endif
  ;

}  // namespace

Engine::Engine(std::optional<std::string> path) noexcept :
    // clang-format off
    binaryDirectory(path ? CommandLine::binary_directory(*path) : ""),
    numaContext(NumaConfig::from_system()),
    options(),
    threads(),
    tt(),
    networks(
      numaContext,
      NNUE::Networks(NNUE::BigNetwork  ({EvalFileDefaultNameBig  , "None", ""}, NNUE::BIG),
                     NNUE::SmallNetwork({EvalFileDefaultNameSmall, "None", ""}, NNUE::SMALL))) {

    options.add("NumaPolicy",           Option("auto", "var none var auto var system var hardware var default", [this](const Option& o) {
        set_numa_config(o);
        return get_numa_config_info_str() + '\n'
             + get_thread_allocation_info_str();
    }));
    options.add("Threads",              Option(MIN_THREADS, MIN_THREADS, MAX_THREADS, [this](const Option&) {
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
    options.add("MultiPV",              Option(DEFAULT_MULTI_PV, 1, MAX_MOVES));
    options.add("SkillLevel",           Option(Skill::MaxLevel, Skill::MinLevel, Skill::MaxLevel));
    options.add("MoveOverhead",         Option(10, 0, 5000));
    options.add("NodesTime",            Option(0, 0, 10000));
    options.add("DrawMoveCount",        Option(DrawMoveCount, 5, 50, [](const Option& o) { DrawMoveCount = int(o); return std::nullopt; }));
    options.add("UCI_Chess960",         Option(Chess960,             [](const Option& o) { Chess960 = bool(o); return std::nullopt; }));
    options.add("UCI_LimitStrength",    Option(false));
    options.add("UCI_ELO",              Option(Skill::MaxELO, Skill::MinELO, Skill::MaxELO));
    options.add("UCI_ShowWDL",          Option(false));
    //options.add("UCI_ShowCurrLine",     Option(false));
    //options.add("UCI_ShowRefutations",  Option(false));
    options.add("NullMovePruning",      Option(true));
    options.add("OwnBook",              Option(false));
    options.add("BookFile",             Option("", [](const Option& o) { Book.init(o); return std::nullopt; }));
    options.add("BookProbeDepth",       Option(100, 1, 256));
    options.add("BookBestPick",         Option(true));
    options.add("SyzygyPath",           Option("", [](const Option& o) { Tablebases::init(o); return std::nullopt; }));
    options.add("SyzygyProbeLimit",     Option(7, 0, 7));
    options.add("SyzygyProbeDepth",     Option(1, 1, 100));
    options.add("Syzygy50MoveRule",     Option(true));
    options.add("SyzygyPVExtend",       Option(true));
    options.add("EvalFileBig",          Option(EvalFileDefaultNameBig  , [this](const Option& o) { load_big_network(o);   return std::nullopt; }));
    options.add("EvalFileSmall",        Option(EvalFileDefaultNameSmall, [this](const Option& o) { load_small_network(o); return std::nullopt; }));
    options.add("ReportMinimal",        Option(false));
    options.add("DebugLogFile",         Option("", [](const Option& o) { start_logger(o); return std::nullopt; }));
    // clang-format on

    load_networks();
    resize_threads_tt();

    setup();
}

Engine::~Engine() noexcept { wait_finish(); }

const Options& Engine::get_options() const noexcept { return options; }
Options&       Engine::get_options() noexcept { return options; }

void Engine::set_numa_config(std::string_view str) noexcept {
    if (str == "none")
        numaContext.set_numa_config(NumaConfig{});

    else if (str == "auto" || str == "system")
        numaContext.set_numa_config(NumaConfig::from_system(true));

    else if (str == "hardware")
        // Don't respect affinity set in the system
        numaContext.set_numa_config(NumaConfig::from_system(false));

    else if (str == "default")
        numaContext.set_numa_config(
          NumaConfig::from_string("0-15,128-143:16-31,144-159:32-47,160-175:48-63,176-191"));

    else
        numaContext.set_numa_config(NumaConfig::from_string(str));

    // Force reallocation of threads in case affinities need to change
    resize_threads_tt();
}

std::string Engine::fen() const noexcept { return pos.fen(); }

void Engine::setup(std::string_view fen, const Strings& moves) noexcept {
    // Drop the old states and create a new one
    states = std::make_unique<StateList>(1);
    pos.set(fen, &states->back());

    std::int16_t ply = 1;
    for (const auto& move : moves)
    {
        Move m = UCI::mix_to_move(move, pos, MoveList<LEGAL>(pos));
        if (m == Move::None)
        {
            UCI::print_info_string("Invalid move in the moves list at " + std::to_string(ply) + ": "
                                   + move);
            break;
        }
        assert(pos.rule50_count() <= 100);
        states->emplace_back();
        pos.do_move(m, states->back());
        ++ply;
    }
}

std::uint64_t Engine::perft(Depth depth, bool detail) noexcept {
    //verify_networks();
    return Benchmark::perft(pos, options["Hash"], threads, depth, detail);
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
    wait_finish();

    Tablebases::init(options["SyzygyPath"]);  // Free mapped files

    if (options["HashRetain"])
        return;

    threads.init();
    tt.init(threads);
}

void Engine::resize_threads_tt() noexcept {
    threads.set(numaContext.numa_config(), {options, networks, threads, tt}, updateContext);
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

void Engine::mirror() noexcept { pos.mirror(); }

std::uint16_t Engine::hashfull(std::uint8_t maxAge) const noexcept { return tt.hashfull(maxAge); }

std::string Engine::get_numa_config_str() const noexcept {
    return numaContext.numa_config().to_string();
}

std::string Engine::get_numa_config_info_str() const noexcept {
    return "Available Processors: " + get_numa_config_str();
}

std::vector<std::pair<std::size_t, std::size_t>> Engine::get_bound_thread_counts() const noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> ratios;

    const auto  threadCounts = threads.get_bound_thread_counts();
    const auto& numaConfig   = numaContext.numa_config();

    NumaIndex numaIdx = 0;
    for (; numaIdx < threadCounts.size(); ++numaIdx)
        ratios.emplace_back(threadCounts[numaIdx], numaConfig.node_cpus_size(numaIdx));
    if (!threadCounts.empty())
        for (; numaIdx < numaConfig.nodes_size(); ++numaIdx)
            ratios.emplace_back(0, numaConfig.node_cpus_size(numaIdx));

    return ratios;
}

std::string Engine::get_thread_binding_info_str() const noexcept {
    std::ostringstream oss;

    auto boundThreadCounts = get_bound_thread_counts();
    if (!boundThreadCounts.empty())
        for (auto itr = boundThreadCounts.begin(); itr != boundThreadCounts.end(); ++itr)
        {
            if (itr != boundThreadCounts.begin())
                oss << ':';
            oss << itr->first << '/' << itr->second;
        }

    return oss.str();
}

std::string Engine::get_thread_allocation_info_str() const noexcept {
    std::ostringstream oss;

    oss << "Threads: " << threads.size();

    auto threadBindingInfoStr = get_thread_binding_info_str();
    if (!threadBindingInfoStr.empty())
        oss << " with NUMA node thread binding: " << threadBindingInfoStr;

    return oss.str();
}

void Engine::verify_networks() const noexcept {
    networks->big.verify(options["EvalFileBig"]);
    networks->small.verify(options["EvalFileSmall"]);
}

void Engine::load_networks() noexcept {
    networks.modify_and_replicate([this](NNUE::Networks& nets) {
        nets.big.load(binaryDirectory, options["EvalFileBig"]);
        nets.small.load(binaryDirectory, options["EvalFileSmall"]);
    });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::load_big_network(std::string_view netFile) noexcept {
    networks.modify_and_replicate([this, &netFile](NNUE::Networks& nets) {
        nets.big.load(binaryDirectory, std::string(netFile));
    });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::load_small_network(std::string_view netFile) noexcept {
    networks.modify_and_replicate([this, &netFile](NNUE::Networks& nets) {
        nets.small.load(binaryDirectory, std::string(netFile));
    });
    threads.init();
    threads.ensure_network_replicated();
}

void Engine::save_networks(const std::array<std::optional<std::string>, 2>& netFiles) noexcept {
    networks.modify_and_replicate([&](const NNUE::Networks& nets) {
        nets.big.save(netFiles[0]);
        nets.small.save(netFiles[1]);
    });
}

void Engine::set_on_update_short(OnUpdateShort&& f) noexcept {
    updateContext.onUpdateShort = std::move(f);
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
