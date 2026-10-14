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

// A lightweight intrusive doubly linked list that does not own its elements or allocate memory.
// Each element embeds a Node that stores pointers to the previous and next nodes, the owning list,
// and the associated element. This allows existing objects (e.g. stack-allocated mutex waiters)
// to be linked directly into the list without allocating separate nodes.
// Unlike std::list, it requires no dynamic allocation.
// Elements must remain alive and at stable addresses while linked, and list operations must
// be externally synchronized when accessed concurrently.
template<typename T>
class IntrusiveList final {
   public:
    struct Node {
        Node*          prev  = nullptr;
        Node*          next  = nullptr;
        IntrusiveList* owner = nullptr;
        T*             data  = nullptr;
    };

    IntrusiveList() noexcept = default;

    [[nodiscard]] T* front() noexcept { return front_ != nullptr ? front_->data : nullptr; }

    [[nodiscard]] const T* front() const noexcept {
        return front_ != nullptr ? front_->data : nullptr;
    }

    [[nodiscard]] T* back() noexcept { return back_ != nullptr ? back_->data : nullptr; }

    [[nodiscard]] const T* back() const noexcept {
        return back_ != nullptr ? back_->data : nullptr;
    }

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));
        assert((back() == nullptr) == (size() == 0));

        return size() == 0;
    }

    [[nodiscard]] bool push_back(Node& node, T& data) noexcept {
        assert(&data.node == &node);
        assert(node.owner == nullptr);
        assert(node.data == nullptr);

        if (&data.node != &node || node.owner != nullptr || node.data != nullptr)
            return false;

        node.data = &data;
        node.prev = back_;
        node.next = nullptr;

        if (back_ != nullptr)
            back_->next = &node;
        else
            front_ = &node;

        back_      = &node;
        node.owner = this;
        ++size_;

        return true;
    }

    [[nodiscard]] bool erase(Node& node) noexcept {
        assert(node.owner == this);

        if (node.owner != this)
            return false;

        if (node.prev != nullptr)
            node.prev->next = node.next;
        else
            front_ = node.next;

        if (node.next != nullptr)
            node.next->prev = node.prev;
        else
            back_ = node.prev;

        node.next  = nullptr;
        node.prev  = nullptr;
        node.owner = nullptr;
        node.data  = nullptr;
        --size_;

        return true;
    }

    [[nodiscard]] const T* next(const T& item) const noexcept {
        assert(item.node.owner == this);

        if (item.node.owner != this)
            return nullptr;

        const Node* node = item.node.next;
        return node != nullptr ? node->data : nullptr;
    }

   private:
    IntrusiveList(const IntrusiveList&)                = delete;
    IntrusiveList& operator=(const IntrusiveList&)     = delete;
    IntrusiveList(IntrusiveList&&) noexcept            = delete;
    IntrusiveList& operator=(IntrusiveList&&) noexcept = delete;

    Node*       front_ = nullptr;
    Node*       back_  = nullptr;
    std::size_t size_  = 0;
};

}  // namespace DON

#endif  // UTILITY_LIST_H_INCLUDED
