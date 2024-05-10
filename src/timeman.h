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
class OptionsMap;

namespace Search {
struct Limits;
}

// The TimeManagement class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManagement final {
   public:
    void init(Search::Limits& limits, const Position& pos, const OptionsMap& options) noexcept;

    TimePoint optimum() const noexcept;
    TimePoint maximum() const noexcept;
    TimePoint elapsed() const noexcept;
    template<typename Func>
    TimePoint elapsed(Func nodes) const noexcept;

    void clear_nodes_time() noexcept;
    void advance_nodes_time(std::int64_t diffNodes) noexcept;

    bool useNodesTime = false;  // True if we are in 'Nodes as Time' mode

   private:
    TimePoint startTime;
    TimePoint optimumTime;
    TimePoint maximumTime;

    std::int64_t totalNodes = -1LL;  // When in 'Nodes as Time' mode
};

inline TimePoint TimeManagement::optimum() const noexcept { return optimumTime; }

inline TimePoint TimeManagement::maximum() const noexcept { return maximumTime; }

inline TimePoint TimeManagement::elapsed() const noexcept { return now() - startTime; }

template<typename Func>
inline TimePoint TimeManagement::elapsed(Func nodes) const noexcept {
    return useNodesTime ? TimePoint(nodes()) : elapsed();
}

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
