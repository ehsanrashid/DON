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

#ifndef UTILITY_LIST_H_INCLUDED
#define UTILITY_LIST_H_INCLUDED

#include <cassert>

#include "../utility/types.h"

namespace DON {

// A lightweight intrusive singly linked list that does not own its elements or allocate memory.
// Each element stores its own next pointer, allowing existing objects
// (e.g. stack-allocated mutex waiters) to be linked directly into the list without creating separate list nodes.
// Unlike std::list, it requires no dynamic allocation and stores only one link pointer per element.
// Elements must remain alive and at stable addresses while linked, and list operations must
// be externally synchronized when accessed concurrently.
template<typename T>
class IntrusiveList final {
   public:
    void push_back(T& item) noexcept {
        item.next = nullptr;

        T*& nextLink = back_ != nullptr ? back_->next : front_;

        nextLink = &item;
        back_    = &item;
        ++size_;
    }

    void erase(T& item) noexcept {
        T*    pre = nullptr;  // Previous node; nullptr means cur is the front
        auto* cur = front_;   // Start searching from the front of the list

        // Find the requested item while tracking its previous node
        while (cur != nullptr && cur != &item)
        {
            pre = cur;
            cur = cur->next;
        }

        // The item is not in the list; nothing to remove
        if (cur == nullptr)
            return;

        // Unlink cur from the list
        if (pre == nullptr)
            front_ = cur->next;  // Front: advance to the next node
        else
            pre->next = cur->next;  // Otherwise, bypass cur

        // If cur was the back, update it to the previous node.
        // This also sets back_ to nullptr when removing the only node.
        if (back_ == cur)
            back_ = pre;

        // Detach the removed item from the list
        cur->next = nullptr;
        // Decrease the number of items
        --size_;
    }

    T* front() const noexcept { return front_; }
    T* back() const noexcept { return back_; }

    usize size() const noexcept { return size_; }

    bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));
        return size() == 0;
    }

   private:
    T*    front_ = nullptr;
    T*    back_  = nullptr;
    usize size_  = 0;
};

}  // namespace DON

#endif  // UTILITY_LIST_H_INCLUDED
