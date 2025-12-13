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
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "evaluate.h"
#include "movegen.h"
#include "nnue/nnue_misc.h"
#include "numa.h"
#include "perft.h"
#include "polybook.h"
#include "shm.h"
#include "syzygy/tbbase.h"
#include "uci.h"

namespace DON {

namespace {

constexpr unsigned MIN_THREADS     = 1U;
const unsigned     MAX_THREADS     = std::max(4U * unsigned(hardware_concurrency()), 1024U);
const unsigned     DEFAULT_THREADS = std::max(1U, MIN_THREADS);

constexpr unsigned MIN_HASH = 1U;
constexpr unsigned MAX_HASH =
#if defined(IS_64BIT)
  0x2000000U
#else
  0x800U
#endif
  ;
constexpr unsigned DEFAULT_HASH = std::max(16U, MIN_HASH);

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
      // Heap-allocate because sizeof(NNUE::Networks) is large
      std::make_unique<NNUE::Networks>(
        std::make_unique<NNUE::BigNetwork>  (NNUE::EvalFile{EvalFileDefaultNameBig  , "None", ""}, NNUE::EmbeddedType::BIG),
        std::make_unique<NNUE::SmallNetwork>(NNUE::EvalFile{EvalFileDefaultNameSmall, "None", ""}, NNUE::EmbeddedType::SMALL))) {

    using OnCng = Option::OnChange;

    options.add("NumaPolicy",           Option("auto", "var none var auto var system var hardware var default", OnCng([this](const Option& o) {
        set_numa_config(o);
        return get_numa_config_info_str() + '\n'
             + get_thread_allocation_info_str();
    })));
    options.add("Threads",              Option(DEFAULT_THREADS, MIN_THREADS, MAX_THREADS, OnCng([this](const Option&) {
        resize_threads_tt();
        return get_thread_allocation_info_str();
    })));
    options.add("Hash",                 Option(DEFAULT_HASH, MIN_HASH, MAX_HASH, OnCng([this](const Option& o) {
        resize_tt(o);
        return "Hash: " + std::to_string(int(o));
    })));
    options.add("Clear Hash",           Option(OnCng([this](const Option&) { init(); return std::nullopt; })));
    options.add("HashRetain",           Option(false));
    options.add("HashFile",             Option(""));
    options.add("Save Hash",            Option(OnCng([this](const Option&) { return save_hash() ? "Save succeeded" : "Save failed"; })));
    options.add("Load Hash",            Option(OnCng([this](const Option&) { return load_hash() ? "Load succeeded" : "Load failed"; })));
    options.add("Ponder",               Option(false));
    options.add("MultiPV",              Option(DEFAULT_MULTI_PV, 1, MAX_MOVES));
    options.add("SkillLevel",           Option(Skill::MAX_LEVEL, Skill::MIN_LEVEL, Skill::MAX_LEVEL));
    options.add("MoveOverhead",         Option(10, 0, 5000));
    options.add("NodesTime",            Option(0, 0, 10000));
    options.add("DrawMoveCount",        Option(Position::DrawMoveCount, 5, 50, OnCng([](const Option& o) { Position::DrawMoveCount = int(o); return std::nullopt; })));
    options.add("UCI_Chess960",         Option(Position::Chess960,             OnCng([](const Option& o) { Position::Chess960 = bool(o); return std::nullopt; })));
    options.add("UCI_LimitStrength",    Option(false));
    options.add("UCI_ELO",              Option(Skill::MAX_ELO, Skill::MIN_ELO, Skill::MAX_ELO));
    options.add("UCI_ShowWDL",          Option(false));
    options.add("OwnBook",              Option(false));
    options.add("BookFile",             Option("", OnCng([](const Option& o) { return Book.load(o) ? "Load succeeded" : "Load failed"; })));
    options.add("BookProbeDepth",       Option(100, 1, 256));
    options.add("BookPickBest",         Option(true));
    options.add("SyzygyPath",           Option("", OnCng([](const Option& o) { Tablebases::init(o); return std::nullopt; })));
    options.add("SyzygyProbeLimit",     Option(7, 0, 7));
    options.add("SyzygyProbeDepth",     Option(1, 1, 100));
    options.add("Syzygy50MoveRule",     Option(true));
    options.add("SyzygyPVExtend",       Option(true));
    options.add("BigEvalFile",          Option(EvalFileDefaultNameBig  , OnCng([this](const Option& o) { load_big_network(o);   return std::nullopt; })));
    options.add("SmallEvalFile",        Option(EvalFileDefaultNameSmall, OnCng([this](const Option& o) { load_small_network(o); return std::nullopt; })));
    options.add("ReportMinimal",        Option(false));
    options.add("LogFile",              Option("", OnCng([](const Option& o) { return Logger::start(o) ? "Logger started" : "Logger not started"; })));
    options.add("Stop Logger",          Option(    OnCng([](const Option&)   { Logger::stop(); return std::nullopt; })));
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
            std::cerr << "Invalid move in the moves list at " << ply << ": " << move << std::endl;
            break;
        }
        assert(pos.rule50_count() <= 100);
        states->emplace_back();
        pos.do_move(m, states->back());
        ++ply;
    }
}

std::uint64_t Engine::perft(Depth depth, bool detail) noexcept {

    State    st;
    Position p;
    p.set(pos, &st);

    return Perft::perft(p, options["Hash"], threads, depth, detail);
}

void Engine::start(const Limit& limit) noexcept {
    assert(!limit.perft);

    verify_networks();
    threads.start(pos, states, limit, options);
}

void Engine::stop() noexcept { threads.stop.store(true, std::memory_order_relaxed); }

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

void Engine::dump(std::optional<std::string_view> dumpFile) const noexcept {

    if (dumpFile.has_value())
    {
        std::ofstream ofs(std::string(dumpFile.value()), std::ios::binary);
        if (ofs.is_open())
        {
            pos.dump(ofs);
            ofs.close();
            return;
        }

        // Couldn't open file - optionally report and fall back
        std::cerr << "Engine::dump: failed to open '" << dumpFile.value()
                  << "', writing to stdout instead" << std::endl;
    }

    // Default: dump to console
    pos.dump(std::cout);
}

void Engine::eval() noexcept {
    verify_networks();
    std::cout << '\n' << Evaluate::trace(pos, *networks) << std::endl;
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
    networks->big.verify(options["BigEvalFile"]);
    networks->small.verify(options["SmallEvalFile"]);

    auto statuses = networks.get_status_and_errors();
    for (std::size_t i = 0; i < statuses.size(); ++i)
    {
        const auto& [status, error] = statuses[i];

        std::string message = "Network replica " + std::to_string(i + 1) + ": ";
        if (status == SystemWideSharedConstantAllocationStatus::NoAllocation)
            message += "No allocation";
        else if (status == SystemWideSharedConstantAllocationStatus::LocalMemory)
            message += "Local memory";
        else if (status == SystemWideSharedConstantAllocationStatus::SharedMemory)
            message += "Shared memory";
        else
            message += "Unknown status";

        if (error.has_value())
            message += ". " + *error;

        UCI::print_info_string(message);
    }
}

void Engine::load_networks() noexcept {
    networks.modify_and_replicate([this](NNUE::Networks& nets) {
        nets.big.load(binaryDirectory, options["BigEvalFile"]);
        nets.small.load(binaryDirectory, options["SmallEvalFile"]);
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

void Engine::save_networks(const StdArray<std::optional<std::string>, 2>& netFiles) noexcept {
    networks.modify_and_replicate([&](const NNUE::Networks& nets) {
        nets.big.save(netFiles[0]);
        nets.small.save(netFiles[1]);
    });
}

bool Engine::save_hash() const noexcept { return tt.save(options["HashFile"]); }

bool Engine::load_hash() noexcept { return tt.load(options["HashFile"], threads); }

void Engine::set_on_update_short(MainSearchManager::OnUpdateShort&& f) noexcept {
    updateContext.onUpdateShort = std::move(f);
}

void Engine::set_on_update_full(MainSearchManager::OnUpdateFull&& f) noexcept {
    updateContext.onUpdateFull = std::move(f);
}

void Engine::set_on_update_iter(MainSearchManager::OnUpdateIter&& f) noexcept {
    updateContext.onUpdateIter = std::move(f);
}

void Engine::set_on_update_move(MainSearchManager::OnUpdateMove&& f) noexcept {
    updateContext.onUpdateMove = std::move(f);
}

}  // namespace DON
