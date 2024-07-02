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

#include <cassert>
#include <memory>
#include <ostream>
#include <sstream>
#include <utility>

#include "evaluate.h"
#include "misc.h"
#include "perft.h"
#include "uci.h"
#include "nnue/nnue_common.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace {
constexpr std::uint32_t MAX_HASH =
#if defined(IS_64BIT)
  0x2000000U;
#else
  0x800U;
#endif

}  // namespace

namespace NN = Eval::NNUE;

Engine::Engine(const std::string& path) noexcept :
    binaryDirectory(CommandLine::get_binary_directory(path)),
    numaContext(NumaConfig::from_system()),
    threads(),
    networks(
      numaContext,
      NN::Networks(
        NN::BigNetwork({EvalFileDefaultNameBig, "None", ""}, NN::EmbeddedNNUEType::BIG),
        NN::SmallNetwork({EvalFileDefaultNameSmall, "None", ""}, NN::EmbeddedNNUEType::SMALL))) {

    // clang-format off
    options["NumaPolicy"] << Option("auto", [this](const Option& o) {
        set_numa_config(o);
        return get_numa_config_info() + "\n" + get_thread_binding_info();
    });
    options["Threads"]      << Option(1, 1, 1024, [this](const Option& o) {
        resize_threads_tt();
        return "Threads: " + std::to_string(int(o)) + "\n" + get_thread_binding_info();
    });
    options["Hash"]         << Option(16, 4, MAX_HASH, [this](const Option& o) {
        resize_tt(o);
        return "Hash: " + std::to_string(int(o));
    });
    options["Clear Hash"]   << Option([this](const Option&) { init(); return std::nullopt; });
    options["Retain Hash"]  << Option(false);
    options["HashFile"]     << Option("<empty>");
    options["Save Hash"]    << Option([this](const Option&) { return std::nullopt; });
    options["Load Hash"]    << Option([this](const Option&) { return std::nullopt; });
    options["Ponder"]       << Option(false);
    options["MultiPV"]      << Option(DefaultMultiPV, 1, std::numeric_limits<std::uint8_t>::max());
    options["Skill Level"]  << Option(Search::Skill::MaxLevel, 0, Search::Skill::MaxLevel);
    options["Move Overhead"] << Option(10, 0, 5000);
    options["NodesTime"]    << Option(0, 0, 10000);
    options["DrawMoveCount"] << Option(Position::DrawMoveCount, 5, 50, [](const Option& o) { Position::DrawMoveCount = o; return std::nullopt; });
    options["UCI_Chess960"] << Option(Position::Chess960, [](const Option& o) { Position::Chess960 = o; return std::nullopt; });
    options["UCI_LimitStrength"] << Option(false);
    options["UCI_ELO"]      << Option(Search::Skill::MinELO, Search::Skill::MinELO, Search::Skill::MaxELO);
    options["UCI_ShowWDL"]  << Option(false);
    options["OwnBook"]      << Option(false);
    options["BookFile"]     << Option("<empty>", [this](const Option& o) { load_book(o); return std::nullopt; });
    options["BookDepth"]    << Option(100, 1, 256);
    options["BookPickBest"] << Option(true);
    options["SyzygyPath"]   << Option("<empty>", [](const Option& o) { Tablebases::init(o); return std::nullopt; });
    options["SyzygyProbeLimit"] << Option(7, 0, 7);
    options["SyzygyProbeDepth"] << Option(1, 1, 100);
    options["Syzygy50MoveRule"] << Option(true);
    options["EvalFileBig"]  << Option(EvalFileDefaultNameBig, [this](const Option& o) { load_big_network(o); return std::nullopt; });
    options["EvalFileSmall"] << Option(EvalFileDefaultNameSmall, [this](const Option& o) { load_small_network(o); return std::nullopt; });
    options["ReportMinimal"] << Option(false);
    options["DebugLogFile"] << Option("<empty>", [](const Option& o) { start_logger(o); return std::nullopt; });
    // clang-format on

    load_networks();
    resize_threads_tt();
}

Engine::~Engine() noexcept { wait_finish(); }

const Options& Engine::get_options() const noexcept { return options; }
Options&       Engine::get_options() noexcept { return options; }

std::string Engine::fen() const noexcept { return pos.fen(); }

void Engine::setup(std::string_view fen, const std::vector<std::string>& moves) noexcept {
    // Drop the old state and create a new one
    states = std::make_unique<StateList>(1);
    pos.set(fen, &states->back());

    for (const std::string& can : moves)
    {
        Move m = UCI::can_to_move(can, pos);
        if (m == Move::None())
            break;
        assert(pos.rule50_count() <= 100);
        states->emplace_back();
        pos.do_move(m, states->back());
    }
}

std::uint64_t Engine::perft(Depth depth, bool detail) noexcept {
    return Benchmark::perft(pos, depth, options["Hash"], threads, detail);
}

void Engine::start(const Search::Limits& limits) noexcept {
    assert(!limits.perft);

    verify_networks();

    threads.start(pos, states, limits, options);
}

void Engine::stop() noexcept { threads.stop = true; }

void Engine::ponderhit() noexcept { threads.main_manager()->ponder = false; }

void Engine::init() noexcept {
    if (options["Retain Hash"])
        return;
    wait_finish();
    threads.init();
    tt.init(threads);
    // @TODO wont work with multiple instances
    Tablebases::init(options["SyzygyPath"]);  // Free mapped files
}

void Engine::wait_finish() const noexcept { threads.main_thread()->wait_finish(); }


void Engine::resize_threads_tt() noexcept {
    threads.set(numaContext.get_numa_config(), {options, networks, threads, tt}, updateContext);
    // Reallocate the hash with the new threadpool size
    resize_tt(options["Hash"]);
}

void Engine::resize_tt(std::size_t mbSize) noexcept {
    wait_finish();
    tt.resize(mbSize, threads);
}

void Engine::load_book(const std::string& bookFile) noexcept {
    wait_finish();
    threads.main_manager()->load_book(bookFile);
}

void Engine::show() const noexcept { sync_cout << pos << sync_endl; }

void Engine::eval() noexcept {

    verify_networks();

    sync_cout << '\n' << Eval::trace(pos, *networks) << sync_endl;
}

void Engine::flip() noexcept { pos.flip(); }

void Engine::set_numa_config(const std::string& str) {
    if (str == "auto" || str == "system")
        numaContext.set_numa_config(NumaConfig::from_system());

    else if (str == "hardware")
        // Don't respect affinity set in the system.
        numaContext.set_numa_config(NumaConfig::from_system(false));

    else if (str == "none")
        numaContext.set_numa_config(NumaConfig{});

    else
        numaContext.set_numa_config(NumaConfig::from_string(str));

    // Force reallocation of threads in case affinities need to change.
    resize_threads_tt();
}

std::vector<std::pair<std::size_t, std::size_t>> Engine::get_bound_thread_counts() const noexcept {
    std::vector<std::pair<std::size_t, std::size_t>> ratios;

    auto              counts  = threads.get_bound_thread_counts();
    const NumaConfig& config  = numaContext.get_numa_config();
    NumaIndex         numaIdx = 0;
    for (; numaIdx < counts.size(); ++numaIdx)
        ratios.emplace_back(counts[numaIdx], config.num_cpus_in_numa_node(numaIdx));
    if (!counts.empty())
        for (; numaIdx < config.num_numa_nodes(); ++numaIdx)
            ratios.emplace_back(0, config.num_cpus_in_numa_node(numaIdx));
    return ratios;
}

std::string Engine::get_numa_config() const noexcept {
    return numaContext.get_numa_config().to_string();
}

std::string Engine::get_numa_config_info() const noexcept {
    std::string numaConfig = get_numa_config();
    return "Available Processors: " + numaConfig;
}

std::string Engine::get_thread_binding_info() const noexcept {
    std::ostringstream oss;

    auto boundThreadCounts = get_bound_thread_counts();
    if (!boundThreadCounts.empty())
    {
        oss << "NUMA Node Thread Binding: ";
        bool isFirst = true;
        for (auto&& [current, total] : boundThreadCounts)
        {
            if (!isFirst)
                oss << ":";
            oss << current << "/" << total;
            isFirst = false;
        }
    }

    return oss.str();
}

void Engine::verify_networks() const noexcept {
    networks->big.verify(options["EvalFileBig"]);
    networks->small.verify(options["EvalFileSmall"]);
}

void Engine::load_networks() noexcept {
    networks.modify_and_replicate([&](NN::Networks& net) {
        net.big.load(binaryDirectory, options["EvalFileBig"]);
        net.small.load(binaryDirectory, options["EvalFileSmall"]);
    });
    threads.init();
}

void Engine::load_big_network(const std::string& bigFile) noexcept {
    networks.modify_and_replicate(
      [&](NN::Networks& net) { net.big.load(binaryDirectory, bigFile); });
    threads.init();
}

void Engine::load_small_network(const std::string& smallFile) noexcept {
    networks.modify_and_replicate(
      [&](NN::Networks& net) { net.small.load(binaryDirectory, smallFile); });
    threads.init();
}

void Engine::save_networks(
  const std::pair<std::optional<std::string>, std::string> files[2]) noexcept {
    networks.modify_and_replicate([&](NN::Networks& net) {
        net.big.save(files[0].first);
        net.small.save(files[1].first);
    });
}

void Engine::set_on_update_end(Search::OnUpdateEnd&& f) noexcept {
    updateContext.onUpdateEnd = std::move(f);
}

void Engine::set_on_update_full(Search::OnUpdateFull&& f) noexcept {
    updateContext.onUpdateFull = std::move(f);
}

void Engine::set_on_update_iter(Search::OnUpdateIter&& f) noexcept {
    updateContext.onUpdateIter = std::move(f);
}

void Engine::set_on_update_move(Search::OnUpdateMove&& f) noexcept {
    updateContext.onUpdateMove = std::move(f);
}

}  // namespace DON
