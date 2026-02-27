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

#include "engine.h"

#include <algorithm>
#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>

#include "evaluate.h"
#include "movegen.h"
#include "numa.h"
#include "perft.h"
#include "polybook.h"
#include "shm.h"
#include "uci.h"
#include "nnue/nmisc.h"
#include "syzygy/tablebase.h"

namespace DON {

namespace {

const std::size_t THREAD_MAX = std::max<std::size_t>(4 * SYSTEM_THREAD_MAX, 1024);

constexpr std::size_t HASH_MAX =
#if defined(IS_64BIT)
  0x2000000U
#else
  0x800U
#endif
  ;

// The default configuration will attempt to group L3 domains up to 32 threads.
// This size was found to be a good balance between the Elo gain of increased
// history sharing and the speed loss from more cross-cache accesses.
// The user can always explicitly override this behavior.
constexpr AutoNumaPolicy NUMA_POLICY_DEFAULT = BundledL3Policy{32};

std::unique_ptr<NNUE::Networks> NETWORKS_DEFAULT(std::string_view binaryDirectory) noexcept {
    auto NetworksDefault =
      std::make_unique<NNUE::Networks>(NNUE::EvalFile{BigEvalFileDefaultName},  //
                                       NNUE::EvalFile{SmallEvalFileDefaultName});

    NetworksDefault->load_big(binaryDirectory);
    NetworksDefault->load_small(binaryDirectory);

    return NetworksDefault;
}

}  // namespace

Engine::Engine(std::string_view path) noexcept :
    // clang-format off
    binaryDirectory(!path.empty() ? CommandLine::binary_directory(path) : std::string{}),
    numaContext(NumaConfig::from_system(NUMA_POLICY_DEFAULT)),
    networks(numaContext, NETWORKS_DEFAULT(binaryDirectory)) {

    using OnCng = Option::OnChange;

    options.add("NumaPolicy",           Option("auto", OnCng([this](const Option& o) {
        set_numa_config(o);
        return numa_config_info() + '\n'
             + thread_allocation();
    })));
    options.add("Threads",              Option(1, 1, THREAD_MAX, OnCng([this](const Option&) {
        resize_threads_tt();
        return thread_allocation();
    })));
    options.add("Hash",                 Option(16, 1, HASH_MAX, OnCng([this](const Option& o) {
        resize_tt(o);
        return "Hash: " + std::to_string(int(o));
    })));
    options.add("Clear Hash",           Option(OnCng([this](const Option&) { init(); return std::nullopt; })));
    options.add("HashRetain",           Option(false));
    options.add("HashFile",             Option(""));
    options.add("Save Hash",            Option(OnCng([this](const Option&) { return save_hash() ? "Save succeeded" : "Save failed"; })));
    options.add("Load Hash",            Option(OnCng([this](const Option&) { return load_hash() ? "Load succeeded" : "Load failed"; })));
    options.add("Ponder",               Option(false));
    options.add("MultiPV",              Option(1, 1, MOVE_MAX));
    options.add("UCI_Chess960",         Option(Position::Chess960, OnCng([](const Option& o) { Position::Chess960 = bool(o); return std::nullopt; })));
    options.add("UCI_LimitStrength",    Option(false));
    options.add("UCI_ELO",              Option(Skill::ELO_MAX, Skill::ELO_MIN, Skill::ELO_MAX));
    options.add("UCI_ShowWDL",          Option(false));
    options.add("SkillLevel",           Option(int(Skill::LEVEL_MAX), int(Skill::LEVEL_MIN), int(Skill::LEVEL_MAX)));
    options.add("OverheadTime",         Option(25,  0, 5000));  // Estimated overhead per move
    options.add("MinMoveTime",          Option(20,  0, 5000));  // Minimum time allowed per move
    options.add("MaxSingleTime",        Option(502, 0, 5000));  // Maximum time allowed for a single move
    options.add("BufferTime",           Option(10,  0, 5000));  // Safety reserve to prevent time trouble
    options.add("TimePercent",          Option(80, 10, 1000));  // Percentage of remaining time to use
    options.add("NodesTime",            Option(0, 0, 10000));
    options.add("SleepOnStart",         Option(false));
    options.add("HistoryLoadFactor",    Option(75, 10, 100, OnCng([this](const Option& o) { historiesMap.max_load_factor(max_load_factor(o / 100.0f)); return std::nullopt; })));
    options.add("DrawMoveCount",        Option(Position::DrawMoveCount, 5, 50, OnCng([](const Option& o) { Position::DrawMoveCount = int(o); return std::nullopt; })));
    options.add("Book",                 Option(false));
    options.add("BookFile",             Option("", OnCng([](const Option& o) { std::string_view bookFile = o;
                                                                               if (bookFile.empty()) return "";
                                                                               return Book.load(bookFile) ? "Load succeeded" : "Load failed"; })));
    options.add("BookProbeDepth",       Option(100, 1, 256));
    options.add("BookPickBest",         Option(true));
    options.add("SyzygyPath",           Option("", OnCng([](const Option& o) { Tablebase::init(o); return std::nullopt; })));
    options.add("SyzygyProbeLimit",     Option(Tablebase::TB_PIECES_MAX, 0, Tablebase::TB_PIECES_MAX));
    options.add("SyzygyProbeDepth",     Option(1, 1, 100));
    options.add("Syzygy50MoveRule",     Option(true));
    options.add("SyzygyPVExtend",       Option(true));
    options.add("BigEvalFile",          Option(BigEvalFileDefaultName  , OnCng([this](const Option& o) { load_big_network(o);   return std::nullopt; })));
    options.add("SmallEvalFile",        Option(SmallEvalFileDefaultName, OnCng([this](const Option& o) { load_small_network(o); return std::nullopt; })));
    options.add("MinimalInfo",          Option(false));
    options.add("LogFile",              Option("", OnCng([](const Option& o) { return Logger::start(o) ? "Logger started" : "Logger not started"; })));
    options.add("Stop Logger",          Option(OnCng([](const Option&) { Logger::stop(); return std::nullopt; })));
    // clang-format on

    resize_threads_tt();

    historiesMap.max_load_factor(max_load_factor(options["HistoryLoadFactor"] / 100.0f));

    setup();
}

Engine::~Engine() noexcept { wait_finish(); }

Options&       Engine::get_options() noexcept { return options; }
const Options& Engine::get_options() const noexcept { return options; }

void Engine::set_numa_config(std::string_view str) noexcept {
    if (str == "none")
        numaContext.set_numa_config(NumaConfig{});
    else if (str == "auto" || str == "system")
        numaContext.set_numa_config(NumaConfig::from_system(NUMA_POLICY_DEFAULT, true));
    else if (str == "hardware")
        // Don't respect affinity set in the system
        numaContext.set_numa_config(NumaConfig::from_system(NUMA_POLICY_DEFAULT, false));
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

    [[maybe_unused]] std::int16_t ply = 1;

    for (const auto& move : moves)
    {
        Move m = UCI::mix_to_move(move, pos, MoveList<GenType::LEGAL>(pos));

        if (m == Move::None)
        {
            DEBUG_LOG("Invalid move in the moves list at " << ply << ": " << move);
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

void Engine::stop() noexcept { threads.request_stop(); }

void Engine::ponderhit() const noexcept { threads.main_manager()->set_ponder(false); }

void Engine::wait_finish() const noexcept { threads.main_thread()->wait_finish(); }

void Engine::init() noexcept {
    wait_finish();

    Tablebase::init(options["SyzygyPath"]);  // Free mapped files

    if (options["HashRetain"])
        return;

    threads.init();
    transpositionTable.init(threads);
}

void Engine::resize_threads_tt() noexcept {

    threads.set(numaContext.numa_config(), sharedState, updateContext);

    // Reallocate the hash with the new thread-pool size
    resize_tt(options["Hash"]);

    threads.ensure_network_replicated();
}

void Engine::resize_tt(std::size_t ttSize) noexcept {
    wait_finish();

    transpositionTable.resize(ttSize, threads);
}

void Engine::show() const noexcept { std::cout << pos << std::endl; }

void Engine::dump(const std::filesystem::path& dumpFile) const noexcept {

    if (!dumpFile.empty())
    {
        if (std::ofstream ofs{dumpFile, std::ios::binary})
        {
            pos.dump(ofs);

            ofs.close();
            return;
        }

        // Couldn't open file - optionally report and fall back
        //DEBUG_LOG("Engine::dump: failed to open '" << *dumpFile << "', writing to stdout instead");
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

std::uint16_t Engine::hashfull(std::uint8_t maxAge) const noexcept {
    return transpositionTable.hashfull(maxAge);
}

std::vector<std::pair<std::size_t, std::size_t>> Engine::bound_thread_counts() const noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> ratios;

    auto  threadCounts = threads.bound_thread_counts();
    auto& numaConfig   = numaContext.numa_config();

    NumaIndex numaIdx = 0;

    while (numaIdx < threadCounts.size())
    {
        ratios.emplace_back(threadCounts[numaIdx], numaConfig.node_cpus_size(numaIdx));
        ++numaIdx;
    }

    if (!threadCounts.empty())
        while (numaIdx < numaConfig.nodes_size())
        {
            ratios.emplace_back(NumaIndex{0}, numaConfig.node_cpus_size(numaIdx));
            ++numaIdx;
        }

    return ratios;
}

std::string Engine::numa_config() const noexcept { return numaContext.numa_config().to_string(); }

std::string Engine::numa_config_info() const noexcept {
    std::string numaConfig{"Available Processors: "};

    numaConfig += numa_config();

    return numaConfig;
}

std::string Engine::thread_binding() const noexcept {
    auto boundThreadCounts = bound_thread_counts();

    std::string threadBinding;
    threadBinding.reserve(8 * boundThreadCounts.size());

    for (const auto& [numaId, threadCount] : boundThreadCounts)
    {
        if (!threadBinding.empty())
            threadBinding.push_back(':');

        threadBinding  //
          .append(std::to_string(numaId))
          .append(1, '/')
          .append(std::to_string(threadCount));
    }

    return threadBinding;
}

std::string Engine::thread_allocation() const noexcept {
    std::string threadAllocation{"Threads: "};
    threadAllocation.append(std::to_string(threads.size()));

    if (std::string threadBinding = thread_binding(); !threadBinding.empty())
        threadAllocation  //
          .append(" with NUMA node thread binding: ")
          .append(threadBinding);

    return threadAllocation;
}

void Engine::verify_networks() const noexcept {

    networks->big.verify(options["BigEvalFile"]);
    networks->small.verify(options["SmallEvalFile"]);

    auto statuses = networks.get_status_and_errors();

    for (std::size_t i = 0; i < statuses.size(); ++i)
    {
        auto& [status, error] = statuses[i];

        std::string message{"Network replica "};
        message  //
          .append(std::to_string(i))
          .append(": ")
          .append(to_string(status));

        if (!error.empty())
            message.append(". ").append(error);

        UCI::print_info_string(message);
    }
}

void Engine::load_networks(const StdArray<std::string_view, 2>& netFiles) noexcept {
    if (!netFiles[0].empty())
        load_big_network(netFiles[0]);
    if (!netFiles[1].empty())
        load_small_network(netFiles[1]);
}

void Engine::load_big_network(std::string_view netFile) noexcept {

    networks.modify_and_replicate([this, &netFile](NNUE::Networks& nets) noexcept {  //
        nets.load_big(binaryDirectory, netFile);
    });

    threads.init();

    threads.ensure_network_replicated();
}

void Engine::load_small_network(std::string_view netFile) noexcept {

    networks.modify_and_replicate([this, &netFile](NNUE::Networks& nets) noexcept {  //
        nets.load_small(binaryDirectory, netFile);
    });

    threads.init();

    threads.ensure_network_replicated();
}

void Engine::save_networks(const StdArray<std::string_view, 2>& netFiles) const noexcept {

    networks->save_big(netFiles[0]);
    networks->save_small(netFiles[1]);
}

bool Engine::load_hash() noexcept {
    return transpositionTable.load(std::string_view{options["HashFile"]}, threads);
}

bool Engine::save_hash() const noexcept {
    return transpositionTable.save(std::string_view{options["HashFile"]});
}

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
