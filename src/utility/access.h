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

#ifndef UTILITY_ACCESS_H_INCLUDED
#define UTILITY_ACCESS_H_INCLUDED

#include <mutex>
#include <shared_mutex>

namespace DON {

template<typename Container, typename Mutex>
class ReadAccess final {
   public:
    using const_iterator = typename Container::const_iterator;

    explicit ReadAccess(Mutex& mutex, const Container& container) :
        sharedLock(mutex),
        container(container) {}

    [[nodiscard]] const_iterator begin() const { return container.begin(); }

    [[nodiscard]] const_iterator end() const { return container.end(); }

   private:
    std::shared_lock<Mutex> sharedLock;
    const Container&        container;
};

template<typename Container, typename Mutex>
class WriteAccess final {
   public:
    using iterator = typename Container::iterator;

    explicit WriteAccess(Mutex& mutex, Container& container) :
        uniqueLock(mutex),
        container(container) {}

    [[nodiscard]] iterator begin() { return container.begin(); }

    [[nodiscard]] iterator end() { return container.end(); }

   private:
    std::unique_lock<Mutex> uniqueLock;
    Container&              container;
};

}  // namespace DON

#endif  // UTILITY_ACCESS_H_INCLUDED
