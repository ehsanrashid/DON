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

#include <memory>
#include <ostream>
#include <utility>
#include <sstream>

#include "evaluate.h"
#include "misc.h"
#include "perft.h"
#include "types.h"
#include "uci.h"
#include "nnue/nnue_common.h"
#include "syzygy/tbprobe.h"

namespace DON {

namespace NN = Eval::NNUE;

Engine::Engine(const std::string& path) noexcept :
    binaryDirectory(CommandLine::get_binary_directory(path)),
    networks(NN::Networks(
      NN::BigNetwork({EvalFileDefaultNameBig, "None", ""}, NN::EmbeddedNNUEType::BIG),
      NN::SmallNetwork({EvalFileDefaultNameSmall, "None", ""}, NN::EmbeddedNNUEType::SMALL))) {}

Engine::~Engine() noexcept { wait_finish(); }

OptionsMap& Engine::get_options() noexcept { return options; }

std::string Engine::fen() const noexcept { return pos.fen(); }

void Engine::setup(std::string_view fen, const std::vector<std::string>& moves) noexcept {
    // Drop the old state and create a new one
    states = std::make_unique<StateList>(1);
    pos.set(fen, &states->back());

    for (const std::string& can : moves)
    {
        Move m = UCI::can_to_move(can, pos);
        if (!m)
            break;
        assert(pos.rule50_count() <= 100);
        states->emplace_back();
        pos.do_move(m, states->back());
    }
}

void Engine::start(const Search::Limits& limits) noexcept {

    if (limits.perft)
    {
        perft(pos, limits.depth, limits.detail);
        return;
    }

    verify_networks();

    threads.start(pos, states, limits, options);
}

void Engine::stop() noexcept { threads.stop = true; }

void Engine::ponderhit() noexcept { threads.main_manager()->ponder = false; }

void Engine::clear() noexcept {
    if (options["Retain Hash"])
        return;
    wait_finish();
    threads.clear();
    tt.clear(options["Threads"]);
    // @TODO wont work with multiple instances
    Tablebases::init(options["SyzygyPath"]);  // Free mapped files
}

void Engine::wait_finish() const noexcept { threads.main_thread()->wait_idle(); }


void Engine::resize_threads() noexcept {
    threads.set({options, networks, threads, tt}, updateContext);
}

void Engine::resize_tt() noexcept {
    wait_finish();
    tt.resize(options["Hash"], options["Threads"]);
}

void Engine::show() const noexcept { sync_cout << pos << sync_endl; }

void Engine::trace_eval() const noexcept {
    StateInfo st;
    ASSERT_ALIGNED(&st, Eval::NNUE::CacheLineSize);

    Position p;
    p.set(pos.fen(), &st);

    verify_networks();

    sync_cout << '\n' << Eval::trace(p, networks) << sync_endl;
}

void Engine::flip() noexcept { pos.flip(); }


void Engine::verify_networks() const noexcept {
    networks.big.verify(options["EvalFileBig"]);
    networks.small.verify(options["EvalFileSmall"]);
}

void Engine::load_networks() noexcept {
    load_big_network(options["EvalFileBig"]);
    load_small_network(options["EvalFileSmall"]);
}

void Engine::load_big_network(const std::string& file) noexcept {
    networks.big.load(binaryDirectory, file);
}

void Engine::load_small_network(const std::string& file) noexcept {
    networks.small.load(binaryDirectory, file);
}

void Engine::save_networks(
  const std::pair<std::optional<std::string>, std::string> files[2]) const noexcept {
    networks.big.save(files[0].first);
    networks.small.save(files[1].first);
}

void Engine::set_on_update_short(Search::OnUpdateShort&& f) noexcept {
    updateContext.onUpdateShort = std::move(f);
}

void Engine::set_on_update_full(Search::OnUpdateFull&& f) noexcept {
    updateContext.onUpdateFull = std::move(f);
}

void Engine::set_on_update_iteration(Search::OnUpdateIteration&& f) noexcept {
    updateContext.onUpdateIteration = std::move(f);
}

void Engine::set_on_update_bestmove(Search::OnUpdateBestMove&& f) noexcept {
    updateContext.onUpdateBestMove = std::move(f);
}

}  // namespace DON
