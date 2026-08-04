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

#ifndef TIMEMAN_H_INCLUDED
#define TIMEMAN_H_INCLUDED

#include "misc.h"
#include "types.h"  // IWYU pragma: keep

namespace DON {

struct Limit;
class Options;

// TimeManager computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManager final {
   public:
    TimeManager() noexcept = default;

    [[nodiscard]] TimePoint optimum() const noexcept;

    [[nodiscard]] TimePoint maximum() const noexcept;

    [[nodiscard]] TimePoint elapsed() const noexcept;

    [[nodiscard]] bool use_nodes_time() const noexcept;

    // NodesFunc&& allows binding to temporaries without copying
    template<typename NodesFunc>
    [[nodiscard]] TimePoint elapsed(NodesFunc&& nodes) const noexcept {
        return use_nodes_time() ? TimePoint(nodes()) : elapsed();
    }

    void reset() noexcept;

    void init(Color ac, i16 ply, i32 moveNum, const Options& options, Limit& limit) noexcept;

    void advance_time_nodes(i64 nodes) noexcept;

   private:
    TimeManager(const TimeManager&) noexcept            = delete;
    TimeManager& operator=(const TimeManager&) noexcept = delete;
    TimeManager(TimeManager&&) noexcept                 = delete;
    TimeManager& operator=(TimeManager&&) noexcept      = delete;

    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    double timeAdjust;

    i64  timeNodes;
    bool useNodesTime;
};

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
