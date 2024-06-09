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

namespace NN = Eval::NNUE;

Engine::Engine(const std::string& path) noexcept :
    binaryDirectory(CommandLine::get_binary_directory(path)),
    numaContext(NumaConfig::from_system()),
    networks(
      numaContext,
      NN::Networks(
        NN::BigNetwork({EvalFileDefaultNameBig, "None", ""}, NN::EmbeddedNNUEType::BIG),
        NN::SmallNetwork({EvalFileDefaultNameSmall, "None", ""}, NN::EmbeddedNNUEType::SMALL))),
    threads() {}

Engine::~Engine() noexcept { wait_finish(); }

Options& Engine::get_options() noexcept { return options; }

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
    tt.init(threads);
    threads.init();
    // @TODO wont work with multiple instances
    Tablebases::init(options["SyzygyPath"]);  // Free mapped files
}

void Engine::wait_finish() const noexcept { threads.main_thread()->wait_finish(); }


void Engine::resize_threads() noexcept {
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
    resize_threads();
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
