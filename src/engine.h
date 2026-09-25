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

#ifndef ENGINE_H_INCLUDED
#define ENGINE_H_INCLUDED

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "history.h"
#include "misc.h"
#include "numa.h"
#include "option.h"
#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "types.h"
#include "nnue/ntypes.h"
#include "nnue/network.h"

namespace DON {

class Engine final {
   public:
    explicit Engine(const fs::path& path = {}) noexcept;
    ~Engine() noexcept;

    Options&       options() noexcept;
    const Options& options() const noexcept;

    std::string fen() const noexcept;

    u64 perft(Depth depth, bool detail = false) const noexcept;

    // Non-blocking call to start searching
    void start(const Limit& limit) const noexcept;
    // Non-blocking call to stop searching
    void stop() const noexcept;

    void ponderhit() const noexcept;

    // Blocking call to wait for search to finish
    void wait_finish() const noexcept;

    // Set a new position, moves are in UCI or SAN format
    std::optional<Error> setup(std::string_view fen   = START_FEN,
                               const Strings&   moves = {}) noexcept;

    void reset() noexcept;

    void set_history_max_load_factor() noexcept;

    void resize_threads_tt() noexcept;

    void resize_tt(usize ttSize) noexcept;

    std::string position() const noexcept;
    std::string evaluation() const noexcept;
    void        dump(const fs::path& dumpPath = {}) const noexcept;

    std::optional<Error> flip() noexcept;
    std::optional<Error> mirror() noexcept;

    u16 hashfull(u8 maxAge = 0) const noexcept;

    bool set_numa_config(std::string_view str) noexcept;

    std::vector<std::pair<usize, usize>> bound_thread_counts() const noexcept;

    std::string numa_config() const noexcept;
    std::string numa_config_info() const noexcept;
    std::string thread_binding() const noexcept;
    std::string thread_allocation() const noexcept;

    /* Network related */

    //
    std::unique_ptr<NNUE::Network> default_network() noexcept;

    void verify_network() const noexcept;

    void load_network(const fs::path& networkPath) noexcept;
    void save_network(const fs::path& networkPath) const noexcept;

    /* Hash related */

    //
    bool load_hash(const fs::path& hashPath) noexcept;
    bool save_hash(const fs::path& hashPath) const noexcept;

    /* On update modifiers */

    //
    void set_on_update_start(Manager::OnUpdateStart&& f) noexcept;
    void set_on_update_short(Manager::OnUpdateShort&& f) noexcept;
    void set_on_update_full(Manager::OnUpdateFull&& f) noexcept;
    void set_on_update_iter(Manager::OnUpdateIter&& f) noexcept;
    void set_on_update_move(Manager::OnUpdateMove&& f) noexcept;

   private:
    // Cannot be movable due to components holding backreferences to fields

    Engine(const Engine&) noexcept            = delete;
    Engine& operator=(const Engine&) noexcept = delete;
    Engine(Engine&&) noexcept                 = delete;
    Engine& operator=(Engine&&) noexcept      = delete;

    const fs::path binaryDirectory;

    NumaReplicationContext                      numaContext;
    NNUE::EvalFile                              networkFile;
    SystemWideLazyNumaReplicated<NNUE::Network> network;
    Options                                     options_;
    Threads                                     threads;
    TranspositionTable                          transpositionTable;
    AtomicHistoriesMap                          atomicHistoriesMap;

    SharedState sharedState{network, options(), transpositionTable, threads, atomicHistoriesMap};

    mutable StateListPtr states;
    Position             pos;

    Manager::UpdateContext updateContext;
};

}  // namespace DON

#endif  // ENGINE_H_INCLUDED
