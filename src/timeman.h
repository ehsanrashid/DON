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
#include "types.h"  // IWYU pragma: keep

namespace DON {

struct Limit;
class Options;

// TimeManager computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManager final {
   public:
    TimeManager() noexcept                              = default;
    TimeManager(const TimeManager&) noexcept            = delete;
    TimeManager(TimeManager&&) noexcept                 = delete;
    TimeManager& operator=(const TimeManager&) noexcept = delete;
    TimeManager& operator=(TimeManager&&) noexcept      = delete;

    TimePoint optimum() const noexcept { return optimumTime; }
    TimePoint maximum() const noexcept { return maximumTime; }
    TimePoint elapsed() const noexcept { return now() - startTime; }
    template<typename Func>
    TimePoint elapsed(Func&& nodes) const noexcept {
        return nodesTimeActive ? TimePoint(nodes()) : elapsed();
    }

    void clear() noexcept;

    void init(Limit&         limit,
              Color          ac,
              std::int16_t   ply,
              std::int32_t   moveNum,
              const Options& options) noexcept;

    void advance_time_nodes(std::int64_t nodes) noexcept;

    bool nodesTimeActive;

   private:
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    double timeAdjust;

    std::int64_t timeNodes;
};

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
