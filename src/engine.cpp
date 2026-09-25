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

#include <cassert>
#include <deque>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>

#include "evaluate.h"
#include "movegen.h"
#include "notation.h"
#include "perft.h"
#include "shm.h"
#include "tablebase/syzygy.h"

namespace DON {

Engine::Engine(const fs::path& path) noexcept :
    // clang-format off
    binaryDirectory(CommandLine::binary_directory(path)),
    numaContext(NumaConfig::from_system(NUMA_POLICY_DEFAULT)),
    networkFile{std::nullopt, {}},
    network(numaContext, default_network()) {

    options().add("NumaPolicy",        OptionFactory::string("auto", OnChange([this](const Option& o) { return set_numa_config(o) ? numa_config_info() + '\n' + thread_allocation() : "NumaPolicy: invalid value '" + std::string(o) + "', keeping previous config."; })));
    options().add("Threads",           OptionFactory::spin(1, 1, int(THREAD_MAX), OnChange([this](const Option&) { resize_threads_tt(); return thread_allocation(); })));
    options().add("Hash",              OptionFactory::spin(16, 1, int(HASH_MAX), OnChange([this](const Option& o) { resize_tt(o); return "Hash: " + std::to_string(int(o)); })));
    options().add("Clear Hash",        OptionFactory::button(OnChange([this](const Option&) { reset(); return std::nullopt; })));
    options().add("HashRetain",        OptionFactory::check(false));
    options().add("HashFile",          OptionFactory::string(""));
    options().add("Save Hash",         OptionFactory::button(OnChange([this](const Option&) { return save_hash(utf8_to_path(options()["HashFile"])) ? "Save succeeded" : "Save failed"; })));
    options().add("Load Hash",         OptionFactory::button(OnChange([this](const Option&) { return load_hash(utf8_to_path(options()["HashFile"])) ? "Load succeeded" : "Load failed"; })));
    options().add("Ponder",            OptionFactory::check(false));
    options().add("MultiPV",           OptionFactory::spin(1, 1, int(MOVE_MAX)));
    options().add("UCI_Chess960",      OptionFactory::check(Position::Chess960, OnChange([](const Option& o) { Position::Chess960 = bool(o); return std::nullopt; })));
    options().add("UCI_LimitStrength", OptionFactory::check(false));
    options().add("UCI_ELO",           OptionFactory::spin(int(Skill::ELOMax), int(Skill::ELOMin), int(Skill::ELOMax)));
    options().add("UCI_ShowWDL",       OptionFactory::check(false));
    options().add("SkillLevel",        OptionFactory::spin(int(Skill::LevelMax), int(Skill::LevelMin), int(Skill::LevelMax)));
    options().add("OverheadTime",      OptionFactory::spin(25,  0, 5000));  // Estimated overhead per move
    options().add("MinMoveTime",       OptionFactory::spin(20,  0, 5000));  // Minimum time allowed per move
    options().add("MaxForcedMoveTime", OptionFactory::spin(500, 0, 5000));  // Maximum time allowed for a forced move
    options().add("BufferTime",        OptionFactory::spin(10,  0, 5000));  // Safety reserve to prevent time trouble
    options().add("TimePercent",       OptionFactory::spin(80, 10, 1000));  // Percentage of remaining time to use
    options().add("NodesTime",         OptionFactory::spin(0, 0, 10000));
    options().add("SleepOnStart",      OptionFactory::check(false));
    options().add("HistoryLoadFactor", OptionFactory::spin(75, 10, 100, OnChange([this](const Option&) { set_history_max_load_factor(); return std::nullopt; })));
    options().add("DrawMoveCount",     OptionFactory::spin(Position::DrawMoveCount, 5, 50, OnChange([](const Option& o) { Position::DrawMoveCount = int(o); return std::nullopt; })));
    options().add("Book",              OptionFactory::check(false));
    options().add("BookFile",          OptionFactory::string("", OnChange([](const Option& o) { return load_book(utf8_to_path(o)) ? "Load succeeded" : "Load failed"; })));
    options().add("BookProbeDepth",    OptionFactory::spin(100, 1, 256));
    options().add("BookBestPick",      OptionFactory::check(true));
    options().add("SyzygyPath",        OptionFactory::string("", OnChange([](const Option& o) { Tablebase::Syzygy::init(o); return std::nullopt; })));
    options().add("SyzygyProbeLimit",  OptionFactory::spin(Tablebase::Syzygy::TB_PIECES_MAX, 0, Tablebase::Syzygy::TB_PIECES_MAX));
    options().add("SyzygyProbeDepth",  OptionFactory::spin(1, 1, 100));
    options().add("Syzygy50MoveRule",  OptionFactory::check(true));
    options().add("SyzygyPVExtend",    OptionFactory::check(true));
    options().add("EvalFile",          OptionFactory::string(EvalFileDefaultName, OnChange([this](const Option& o) { load_network(utf8_to_path(o)); return std::nullopt; })));
    options().add("MinimalInfo",       OptionFactory::check(false));
    options().add("LogFile",           OptionFactory::string("", OnChange([](const Option& o) { return Logger::start(utf8_to_path(o)) ? "Logger started" : "Logger not started"; })));
    options().add("Stop Logger",       OptionFactory::button(OnChange([](const Option&) { Logger::stop(); return std::nullopt; })));
    // clang-format on

    set_history_max_load_factor();

    resize_threads_tt();

    setup();
}

Engine::~Engine() noexcept { wait_finish(); }

Options&       Engine::options() noexcept { return options_; }
const Options& Engine::options() const noexcept { return options_; }

std::string Engine::fen() const noexcept { return pos.fen(); }

u64 Engine::perft(const Depth depth, const bool detail) const noexcept {
    return Perft::perft(pos, threads, options()["Hash"], depth, detail);
}

void Engine::start(const Limit& limit) const noexcept {
    assert(!limit.perft);

    verify_network();

    threads.start(pos, std::move(states), limit, options());
}

void Engine::stop() const noexcept { threads.request_stop(); }

void Engine::ponderhit() const noexcept {
    if (auto* const manager = threads.manager(); manager != nullptr)
        manager->set_ponder(false);
}

void Engine::wait_finish() const noexcept {
    if (auto* const mainThread = threads.main_thread(); mainThread != nullptr)
        mainThread->wait_finish();
}

std::optional<Error> Engine::setup(const std::string_view fen, const Strings& moves) noexcept {
    // Drop the old states and create a new one
    states = std::make_unique<StateList>(1);

    if (const auto err = pos.set(fen, &states->back()))
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

void Engine::reset() noexcept {
    wait_finish();

    Position::reset();
    Tablebase::Syzygy::init(options()["SyzygyPath"]);  // Free mapped files

    if (options()["HashRetain"])
        return;

    threads.reset();
    transpositionTable.reset(threads);
}

void Engine::set_history_max_load_factor() noexcept {
    wait_finish();

    atomicHistoriesMap.clear();
    atomicHistoriesMap.max_load_factor(max_load_factor(options()["HistoryLoadFactor"] / 100.0f));
    atomicHistoriesMap.rehash(0);
}

void Engine::resize_threads_tt() noexcept {
    wait_finish();

    threads.set(numaContext.numa_config(), sharedState, updateContext);

    // Reallocate the hash with the new thread-pool size
    resize_tt(options()["Hash"]);

    threads.ensure_network_replicated();
}

void Engine::resize_tt(const usize ttSize) noexcept {
    wait_finish();

    transpositionTable.resize(ttSize, threads);
}

std::string Engine::position() const noexcept {
    std::ostringstream oss;
    oss << pos;
    return oss.str();
}

std::string Engine::evaluation() const noexcept {
    verify_network();

    return Evaluate::trace(pos, *network);
}

void Engine::dump(const fs::path& dumpPath) const noexcept {

    if (!dumpPath.empty())
    {
        if (std::ofstream ofs{dumpPath, std::ios::binary})
        {
            pos.dump(ofs);

            ofs.close();
            return;
        }

        // Couldn't open file - optionally report and fall back
        //DEBUG_LOG("Engine::dump: failed to open '" << *dumpPath << "', writing to stdout instead");
    }

    // Default: dump to console
    pos.dump(std::cout);
}

std::optional<Error> Engine::flip() noexcept { return pos.flip(); }

std::optional<Error> Engine::mirror() noexcept { return pos.mirror(); }

u16 Engine::hashfull(const u8 maxAge) const noexcept { return transpositionTable.hashfull(maxAge); }

bool Engine::set_numa_config(const std::string_view cfg) noexcept {
    NumaConfig numaCfg;

    if (cfg == "none")
        numaCfg = NumaConfig{};
    else if (cfg == "auto" || cfg == "system")
        numaCfg = NumaConfig::from_system(NUMA_POLICY_DEFAULT, true);
    else if (cfg == "hardware")
        // Don't respect affinity set in the system
        numaCfg = NumaConfig::from_system(NUMA_POLICY_DEFAULT, false);
    else
    {
        auto config = NumaConfig::from_string(cfg);
        if (!config)
            return false;

        numaCfg = std::move(*config);
    }

    numaContext.set_numa_config(std::move(numaCfg));

    // Force reallocation of threads in case affinities need to change
    resize_threads_tt();
    return true;
}

std::vector<std::pair<usize, usize>> Engine::bound_thread_counts() const noexcept {
    std::vector<std::pair<usize, usize>> ratios;

    auto  threadCounts = threads.bound_thread_counts();
    auto& numaConfig   = numaContext.numa_config();

    usize numaIdx = 0;

    for (; numaIdx < threadCounts.size(); ++numaIdx)
        ratios.emplace_back(threadCounts[numaIdx], numaConfig.node_cpus_size(NumaIndex(numaIdx)));
    // Threads: 1 with NUMA node thread binding: 0/32
    if (!threadCounts.empty())
        for (; numaIdx < numaConfig.nodes_size(); ++numaIdx)
            ratios.emplace_back(usize{0}, numaConfig.node_cpus_size(NumaIndex(numaIdx)));

    return ratios;
}

std::string Engine::numa_config() const noexcept { return numaContext.numa_config().to_string(); }

std::string Engine::numa_config_info() const noexcept {
    std::string numaConfig{"Available Processors: "};

    numaConfig += numa_config();

    return numaConfig;
}

std::string Engine::thread_binding() const noexcept {
    std::string threadBinding;

    auto boundThreadCounts = bound_thread_counts();

    threadBinding.reserve(8 * boundThreadCounts.size());

    for (const auto& [numaId, threadCount] : boundThreadCounts)
    {
        threadBinding.append(std::to_string(numaId)).push_back('/');
        threadBinding.append(std::to_string(threadCount)).push_back(':');
    }

    if (!threadBinding.empty())
        threadBinding.pop_back();

    return threadBinding;
}

std::string Engine::thread_allocation() const noexcept {
    auto threadAllocation = std::string{"Threads: "};
    threadAllocation.append(std::to_string(threads.size()));

    if (const auto threadBinding = thread_binding(); !threadBinding.empty())
        threadAllocation.append(" with NUMA node thread binding: ").append(threadBinding);

    return threadAllocation;
}

std::unique_ptr<NNUE::Network> Engine::default_network() noexcept {
    auto defaultNetwork = std::make_unique<NNUE::Network>();

    defaultNetwork->load(binaryDirectory, fs::path{}, networkFile);

    return defaultNetwork;
}

void Engine::verify_network() const noexcept {

    auto evalPath = utf8_to_path(options()["EvalFile"]);

    network->verify(evalPath, networkFile);

    auto statuses = network.get_status_and_errors();

    for (usize i = 0; i < statuses.size(); ++i)
    {
        auto& [status, error] = statuses[i];

        auto message = std::string{"Network replica "}
                         .append(std::to_string(i))
                         .append(": ")
                         .append(to_string(status));

        if (!error.empty())
        {
            message.push_back(' ');
            message.append(error);
        }

        print_info_string(message);
    }
}

void Engine::load_network(const fs::path& networkPath) noexcept {

    network.modify_and_replicate([this, &networkPath](NNUE::Network& net) noexcept {  //
        net.load(binaryDirectory, networkPath, networkFile);
    });

    threads.reset();

    threads.ensure_network_replicated();
}

void Engine::save_network(const fs::path& networkPath) const noexcept {
    network->save(networkPath, networkFile);
}

bool Engine::load_hash(const fs::path& hashPath) noexcept {
    return transpositionTable.load(hashPath, threads);
}

bool Engine::save_hash(const fs::path& hashPath) const noexcept {
    return transpositionTable.save(hashPath);
}

void Engine::set_on_update_start(Manager::OnUpdateStart&& f) noexcept {
    updateContext.onUpdateStart = std::move(f);
}

void Engine::set_on_update_short(Manager::OnUpdateShort&& f) noexcept {
    updateContext.onUpdateShort = std::move(f);
}

void Engine::set_on_update_full(Manager::OnUpdateFull&& f) noexcept {
    updateContext.onUpdateFull = std::move(f);
}

void Engine::set_on_update_iter(Manager::OnUpdateIter&& f) noexcept {
    updateContext.onUpdateIter = std::move(f);
}

void Engine::set_on_update_move(Manager::OnUpdateMove&& f) noexcept {
    updateContext.onUpdateMove = std::move(f);
}

}  // namespace DON
