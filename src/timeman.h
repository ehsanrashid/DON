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

// TimeManager class computes the optimal time to think depending on
// the maximum available time, the game move number, and other parameters.
class TimeManager final {
   public:
    TimeManager() noexcept;
    void init(Search::Limits& limits, const Position& pos, const OptionsMap& options) noexcept;

    TimePoint optimum() const noexcept;
    TimePoint maximum() const noexcept;
    TimePoint elapsed() const noexcept;
    template<typename Func>
    TimePoint elapsed(Func nodes) const noexcept;

    void clear() noexcept;

    bool          use_nodes_time() const noexcept;
    std::uint64_t remain_nodes() const noexcept;
    void          advance(std::int64_t usedNodes) noexcept;

   private:
    TimePoint initialTime;

    TimePoint optimumTime;
    TimePoint maximumTime;

    double initialAdjust;

    TimePoint     nodesTime;
    std::uint64_t remainNodes;
};

inline TimeManager::TimeManager() noexcept { clear(); }

inline TimePoint TimeManager::optimum() const noexcept { return optimumTime; }

inline TimePoint TimeManager::maximum() const noexcept { return maximumTime; }

inline TimePoint TimeManager::elapsed() const noexcept { return now() - initialTime; }

template<typename Func>
inline TimePoint TimeManager::elapsed(Func nodes) const noexcept {
    return use_nodes_time() ? TimePoint(nodes()) : elapsed();
}

inline void TimeManager::clear() noexcept {
    optimumTime = 0;
    maximumTime = 0;

    initialAdjust = -1.0;

    nodesTime   = 0;
    remainNodes = 0;
}

inline bool TimeManager::use_nodes_time() const noexcept { return nodesTime != 0; }

inline std::uint64_t TimeManager::remain_nodes() const noexcept { return remainNodes - 1ULL; }

}  // namespace DON

#endif  // #ifndef TIMEMAN_H_INCLUDED
