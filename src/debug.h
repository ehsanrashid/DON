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

#ifndef DEBUG_H_INCLUDED
#define DEBUG_H_INCLUDED

#include "misc.h"

namespace DON {

// Debug functions used mainly to collect run-time statistics
namespace Debug {

void clear() noexcept;

void hit_on(bool cond, usize slot = 0) noexcept;

void min_of(i64 value, usize slot = 0) noexcept;

void max_of(i64 value, usize slot = 0) noexcept;

void extreme_of(i64 value, usize slot = 0) noexcept;

void mean_of(i64 value, usize slot = 0) noexcept;

void stdev_of(i64 value, usize slot = 0) noexcept;

void correl_of(i64 value1, i64 value2, usize slot = 0) noexcept;

void print() noexcept;

}  // namespace Debug

}  // namespace DON

#endif  // DEBUG_H_INCLUDED
