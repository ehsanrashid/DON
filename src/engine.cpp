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
#include <optional>

#include "evaluate.h"
#include "movegen.h"
#include "notation.h"
#include "perft.h"
#include "shm.h"
#include "book/polyglot.h"
#include "tablebase/syzygy.h"

namespace DON {

namespace {

const usize THREAD_MAX = std::max<usize>(4 * SYSTEM_THREAD_MAX, 1024);

constexpr usize HASH_MAX =
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

}  // namespace

Engine::Engine(const std::filesystem::path& path) noexcept :
    // clang-format off
    binaryDirectory(CommandLine::binary_directory(path)),
    numaContext(NumaConfig::from_system(NUMA_POLICY_DEFAULT)),
    networkFile{std::nullopt, {}},
    network(numaContext, default_network()) {

    using OnCng = Option::OnChange;

    options.add("NumaPolicy",        Option("auto", OnCng([this](const Option& o) { return set_numa_config(o) ? numa_config_info() + '\n' + thread_allocation() : "NumaPolicy: invalid value '" + std::string(o) + "', keeping previous config."; })));
    options.add("Threads",           Option(1, 1, int(THREAD_MAX), OnCng([this](const Option&) { resize_threads_tt(); return thread_allocation(); })));
    options.add("Hash",              Option(16, 1, int(HASH_MAX), OnCng([this](const Option& o) { resize_tt(o); return "Hash: " + std::to_string(int(o)); })));
    options.add("Clear Hash",        Option(OnCng([this](const Option&) { reset(); return std::nullopt; })));
    options.add("HashRetain",        Option(false));
    options.add("HashFile",          Option(""));
    options.add("Save Hash",         Option(OnCng([this](const Option&) { return save_hash(path_from_utf8(options["HashFile"])) ? "Save succeeded" : "Save failed"; })));
    options.add("Load Hash",         Option(OnCng([this](const Option&) { return load_hash(path_from_utf8(options["HashFile"])) ? "Load succeeded" : "Load failed"; })));
    options.add("Ponder",            Option(false));
    options.add("MultiPV",           Option(1, 1, int(MOVE_MAX)));
    options.add("UCI_Chess960",      Option(Position::Chess960, OnCng([](const Option& o) { Position::Chess960 = bool(o); return std::nullopt; })));
    options.add("UCI_LimitStrength", Option(false));
    options.add("UCI_ELO",           Option(int(Skill::ELOMax), int(Skill::ELOMin), int(Skill::ELOMax)));
    options.add("UCI_ShowWDL",       Option(false));
    options.add("SkillLevel",        Option(int(Skill::LevelMax), int(Skill::LevelMin), int(Skill::LevelMax)));
    options.add("OverheadTime",      Option(25,  0, 5000));  // Estimated overhead per move
    options.add("MinMoveTime",       Option(20,  0, 5000));  // Minimum time allowed per move
    options.add("MaxForcedMoveTime", Option(500, 0, 5000));  // Maximum time allowed for a forced move
    options.add("BufferTime",        Option(10,  0, 5000));  // Safety reserve to prevent time trouble
    options.add("TimePercent",       Option(80, 10, 1000));  // Percentage of remaining time to use
    options.add("NodesTime",         Option(0, 0, 10000));
    options.add("SleepOnStart",      Option(false));
    options.add("HistoryLoadFactor", Option(75, 10, 100, OnCng([this](const Option&) { set_history_max_load_factor(); return std::nullopt; })));
    options.add("DrawMoveCount",     Option(Position::DrawMoveCount, 5, 50, OnCng([](const Option& o) { Position::DrawMoveCount = int(o); return std::nullopt; })));
    options.add("Book",              Option(false));
    options.add("BookFile",          Option("", OnCng([](const Option& o) { auto bookFile = path_from_utf8(o); if (bookFile.empty()) return ""; return pgBook.load(bookFile) ? "Load succeeded" : "Load failed"; })));
    options.add("BookProbeDepth",    Option(100, 1, 256));
    options.add("BookBestPick",      Option(true));
    options.add("SyzygyPath",        Option("", OnCng([](const Option& o) { Tablebase::Syzygy::init(o); return std::nullopt; })));
    options.add("SyzygyProbeLimit",  Option(Tablebase::Syzygy::TB_PIECES_MAX, 0, Tablebase::Syzygy::TB_PIECES_MAX));
    options.add("SyzygyProbeDepth",  Option(1, 1, 100));
    options.add("Syzygy50MoveRule",  Option(true));
    options.add("SyzygyPVExtend",    Option(true));
    options.add("EvalFile",          Option(EvalFileDefaultName, OnCng([this](const Option& o) { load_network(path_from_utf8(o)); return std::nullopt; })));
    options.add("MinimalInfo",       Option(false));
    options.add("LogFile",           Option("", OnCng([](const Option& o) { return Logger::start(path_from_utf8(o)) ? "Logger started" : "Logger not started"; })));
    options.add("Stop Logger",       Option(OnCng([](const Option&) { Logger::stop(); return std::nullopt; })));
    // clang-format on

    set_history_max_load_factor();

    resize_threads_tt();

    setup();
}

Engine::~Engine() noexcept { wait_finish(); }

Options&       Engine::get_options() noexcept { return options; }
const Options& Engine::get_options() const noexcept { return options; }

std::string Engine::fen() const noexcept { return pos.fen(); }

std::optional<Error> Engine::setup(const std::string_view fen, const Strings& moves) noexcept {
    // Drop the old states and create a new one
    states = std::make_unique<StateList>(1);

    if (auto err = pos.set(fen, &states->back()))
        return err;

    i16 ply = 1;
    for (const auto& move : moves)
    {
        const Move m = mix_to_move(move, pos, MoveList<GenType::LEGAL>(pos));

        if (m == Move::None)
            return Error{"Invalid move at ply " + std::to_string(ply) + ": " + move};

        if (pos.rule50_count() > RULE50_COUNT_MAX)
            return Error{"Invalid position: 50-move rule count exceeds the allowed range: "
                         + std::to_string(pos.rule50_count())};

        states->emplace_back();
        pos.do_move(m, states->back());

        ++ply;
    }

    return std::nullopt;
}

u64 Engine::perft(const Depth depth, const bool detail) noexcept {

    State    st;
    Position p;
    p.set(pos, &st);

    return Perft::perft(p, options["Hash"], threads, depth, detail);
}

void Engine::start(const Limit& limit) noexcept {
    assert(!limit.perft);

    verify_network();

    threads.start(pos, states, limit, options);
}

void Engine::stop() noexcept { threads.request_stop(); }

void Engine::ponderhit() const noexcept {
    auto* mainManager = threads.main_manager();
    if (mainManager != nullptr)
        mainManager->set_ponder(false);
}

void Engine::wait_finish() const noexcept {
    auto* mainThread = threads.main_thread();
    if (mainThread != nullptr)
        mainThread->wait_finish();
}

void Engine::reset() noexcept {
    wait_finish();

    Tablebase::Syzygy::init(options["SyzygyPath"]);  // Free mapped files

    if (options["HashRetain"])
        return;

    threads.reset();
    transpositionTable.reset(threads);
}

void Engine::set_history_max_load_factor() noexcept {
    atomicHistoriesMap.max_load_factor(max_load_factor(options["HistoryLoadFactor"] / 100.0f));
}

void Engine::resize_threads_tt() noexcept {
    wait_finish();

    threads.set(numaContext.numa_config(), sharedState, updateContext);

    // Reallocate the hash with the new thread-pool size
    resize_tt(options["Hash"]);

    threads.ensure_network_replicated();
}

void Engine::resize_tt(const usize ttSize) noexcept {
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
    verify_network();

    std::cout << '\n' << Evaluate::trace(pos, *network) << std::endl;
}

std::optional<Error> Engine::flip() noexcept { return pos.flip(); }

std::optional<Error> Engine::mirror() noexcept { return pos.mirror(); }

u16 Engine::hashfull(const u8 maxAge) const noexcept { return transpositionTable.hashfull(maxAge); }

bool Engine::set_numa_config(const std::string_view cfg) noexcept {
    if (cfg == "none")
        numaContext.set_numa_config(NumaConfig{});
    else if (cfg == "auto" || cfg == "system")
        numaContext.set_numa_config(NumaConfig::from_system(NUMA_POLICY_DEFAULT, true));
    else if (cfg == "hardware")
        // Don't respect affinity set in the system
        numaContext.set_numa_config(NumaConfig::from_system(NUMA_POLICY_DEFAULT, false));
    else
    {
        auto numaCfg = NumaConfig::from_string(cfg);
        if (!numaCfg)
            return false;

        numaContext.set_numa_config(std::move(*numaCfg));
    }

    // Force reallocation of threads in case affinities need to change
    resize_threads_tt();
    return true;
}

std::vector<std::pair<usize, usize>> Engine::bound_thread_counts() const noexcept {
    std::vector<std::pair<usize, usize>> ratios;

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

    if (const std::string threadBinding = thread_binding(); !threadBinding.empty())
        threadAllocation  //
          .append(" with NUMA node thread binding: ")
          .append(threadBinding);

    return threadAllocation;
}

std::unique_ptr<NNUE::Network> Engine::default_network() noexcept {
    auto defaultNetwork = std::make_unique<NNUE::Network>();

    defaultNetwork->load(binaryDirectory, std::filesystem::path{}, networkFile);

    return defaultNetwork;
}

void Engine::verify_network() const noexcept {

    auto evalFilePath = path_from_utf8(options["EvalFile"]);

    network->verify(evalFilePath, networkFile);

    auto statuses = network.get_status_and_errors();

    for (usize i = 0; i < statuses.size(); ++i)
    {
        auto& [status, error] = statuses[i];

        std::string message{"Network replica "};
        message  //
          .append(std::to_string(i))
          .append(": ")
          .append(to_string(status));

        if (!error.empty())
            message.append(". ").append(error);

        print_info_string(message);
    }
}

void Engine::load_network(const std::filesystem::path& networkFilePath) noexcept {

    network.modify_and_replicate([this, &networkFilePath](NNUE::Network& net) noexcept {  //
        net.load(binaryDirectory, networkFilePath, networkFile);
    });

    threads.reset();

    threads.ensure_network_replicated();
}

void Engine::save_network(const std::filesystem::path& networkFilePath) const noexcept {
    network->save(networkFilePath, networkFile);
}

bool Engine::load_hash(const std::filesystem::path& hashFile) noexcept {
    return transpositionTable.load(hashFile, threads);
}

bool Engine::save_hash(const std::filesystem::path& hashFile) const noexcept {
    return transpositionTable.save(hashFile);
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
