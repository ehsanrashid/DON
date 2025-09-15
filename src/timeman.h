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

namespace DON {

class Position;
class Options;
struct Limit;

// TimeManager class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManager final {
   public:
    TimeManager() noexcept { init(); }

    TimeManager(const TimeManager&) noexcept            = delete;
    TimeManager(TimeManager&&) noexcept                 = delete;
    TimeManager& operator=(const TimeManager&) noexcept = delete;
    TimeManager& operator=(TimeManager&&) noexcept      = delete;

    TimePoint optimum() const noexcept { return optimumTime; }
    TimePoint maximum() const noexcept { return maximumTime; }
    TimePoint elapsed() const noexcept { return now() - startTime; }
    template<typename Func>
    TimePoint elapsed(Func nodes) const noexcept {
        return use_nodes_time() ? TimePoint(nodes()) : elapsed();
    }

    void init() noexcept {

        initialAdjust = -1.0;

        optimumTime = 0;
        maximumTime = 0;

        nodesTime   = 0;
        remainNodes = 0;
    }
    void init(const Position& pos, const Options& options, Limit& limit) noexcept;

    bool use_nodes_time() const noexcept { return bool(nodesTime); }

    auto remain_nodes() const noexcept { return remainNodes - OffsetNode; }

    void update_nodes(std::int64_t usedNodes) noexcept;

   private:
    static constexpr std::uint64_t OffsetNode = 1;

    TimePoint startTime;

    double initialAdjust;

    TimePoint optimumTime;
    TimePoint maximumTime;

    TimePoint     nodesTime;
    std::uint64_t remainNodes;
};

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
