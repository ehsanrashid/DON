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

#ifndef TIMEMAN_H_INCLUDED
#define TIMEMAN_H_INCLUDED

#include <cstdint>

#include "misc.h"
#include "types.h"

namespace DON {

class Position;
class Options;
struct Limit;

// TimeManager class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManager final {
   public:
    TimeManager() noexcept { clear(); }

    TimeManager(const TimeManager&) noexcept            = delete;
    TimeManager(TimeManager&&) noexcept                 = delete;
    TimeManager& operator=(const TimeManager&) noexcept = delete;
    TimeManager& operator=(TimeManager&&) noexcept      = delete;

    void init(Limit& limit, const Position& pos, const Options& options) noexcept;

    TimePoint optimum() const noexcept { return optimumTime; }
    TimePoint maximum() const noexcept { return maximumTime; }
    TimePoint elapsed() const noexcept { return now() - startTime; }
    template<typename Func>
    TimePoint elapsed(Func nodes) const noexcept {
        return use_nodes_time() ? TimePoint(nodes()) : elapsed();
    }

    void clear() noexcept {
        optimumTime = 0;
        maximumTime = 0;

        initialAdjust = MIN_ADJUST;

        nodesTime   = 0;
        remainNodes = 0;
    }

    bool use_nodes_time() const noexcept { return bool(nodesTime); }

    auto remain_nodes() const noexcept { return remainNodes - NODE_OFFSET; }

    void advance_nodes(std::int64_t usedNodes) noexcept;

   private:
    static constexpr std::uint64_t NODE_OFFSET = 1ull;
    static constexpr double        MIN_ADJUST  = 1e-6;

    TimePoint startTime;

    TimePoint optimumTime;
    TimePoint maximumTime;

    double initialAdjust;

    TimePoint     nodesTime;
    std::uint64_t remainNodes;
};

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
