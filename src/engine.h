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

#ifndef ENGINE_H_INCLUDED
#define ENGINE_H_INCLUDED

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "position.h"
#include "search.h"
#include "thread.h"
#include "tt.h"
#include "types.h"
#include "ucioption.h"
#include "numa.h"
#include "nnue/network.h"

namespace DON {

class Engine final {
   public:
    explicit Engine(std::optional<std::string> path = std::nullopt) noexcept;
    ~Engine() noexcept;

    // Cannot be movable due to components holding backreferences to fields
    Engine(const Engine&)            = delete;
    Engine(Engine&&)                 = delete;
    Engine& operator=(const Engine&) = delete;
    Engine& operator=(Engine&&)      = delete;

    const Options& get_options() const noexcept;
    Options&       get_options() noexcept;

    std::string fen() const noexcept;

    // Set a new position, moves are in UCI or SAN format
    void setup(std::string_view fen, const std::deque<std::string>& moves = {}) noexcept;

    std::uint64_t perft(Depth depth, bool detail = false) noexcept;
    // Non-blocking call to start searching
    void start(const Limit& limit) noexcept;
    // Non-blocking call to stop searching
    void stop() noexcept;

    void ponderhit() noexcept;

    // Blocking call to wait for search to finish
    void wait_finish() const noexcept;

    void set_numa_config(const std::string& str);

    void resize_threads_tt() noexcept;

    void resize_tt(std::size_t ttSize) noexcept;

    void load_book(const std::string& bookFile) noexcept;

    void init() noexcept;

    void show() const noexcept;
    void eval() noexcept;
    void flip() noexcept;

    std::uint16_t get_hashfull(std::uint16_t maxAge = 0) const noexcept;

    std::vector<std::pair<std::size_t, std::size_t>>  //
                get_bound_thread_counts() const noexcept;
    std::string get_numa_config() const noexcept;
    std::string get_numa_config_info() const noexcept;
    std::string get_thread_allocation_info() const noexcept;
    std::string get_thread_binding_info() const noexcept;

    // Network related
    void verify_networks() const noexcept;
    void load_networks() noexcept;
    void load_big_network(const std::string& bigFile) noexcept;
    void load_small_network(const std::string& smallFile) noexcept;
    void save_networks(const std::array<std::optional<std::string>, 2>& files) noexcept;

    void set_on_update_end(OnUpdateEnd&& f) noexcept;
    void set_on_update_full(OnUpdateFull&& f) noexcept;
    void set_on_update_iter(OnUpdateIter&& f) noexcept;
    void set_on_update_move(OnUpdateMove&& f) noexcept;

    void set_on_verify_networks(std::function<void(std::string_view)>&&) noexcept;

   private:
    const std::string binaryDirectory;

    Position                              pos;
    StateListPtr                          states;
    Options                               options;
    ThreadPool                            threads;
    TranspositionTable                    tt;
    NumaReplicationContext                numaContext;
    LazyNumaReplicated<NNUE::Networks>    networks;
    UpdateContext                         updateContext;
    std::function<void(std::string_view)> onVerifyNetworks;
};

}  // namespace DON

#endif  // #ifndef ENGINE_H_INCLUDED