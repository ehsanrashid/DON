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
// Each element stores pointers to the previous and next elements, allowing existing objects
// (e.g. stack-allocated mutex waiters) to be linked directly into the list without allocating
// separate nodes. Unlike std::list, it requires no dynamic allocation.
// Elements must remain alive and at stable addresses while linked, and list operations must
// be externally synchronized when accessed concurrently.
// Unlike OwnedIntrusiveList, it does not track list ownership for individual elements.
template<typename T>
class IntrusiveList final {
   public:
    IntrusiveList() noexcept                           = default;
    IntrusiveList(const IntrusiveList&)                = delete;
    IntrusiveList& operator=(const IntrusiveList&)     = delete;
    IntrusiveList(IntrusiveList&&) noexcept            = delete;
    IntrusiveList& operator=(IntrusiveList&&) noexcept = delete;

    [[nodiscard]] T* front() noexcept { return front_; }

    [[nodiscard]] const T* front() const noexcept { return front_; }

    [[nodiscard]] T* back() noexcept { return back_; }

    [[nodiscard]] const T* back() const noexcept { return back_; }

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));
        assert((back() == nullptr) == (size() == 0));
        return size() == 0;
    }

    [[nodiscard]] bool push_front(T& item) noexcept {
        assert(item.prev == nullptr);
        assert(item.next == nullptr);

        if (item.prev != nullptr || item.next != nullptr)
            return false;

        attach_front(item);

        return true;
    }

    [[nodiscard]] bool push_back(T& item) noexcept {
        assert(item.prev == nullptr);
        assert(item.next == nullptr);

        if (item.prev != nullptr || item.next != nullptr)
            return false;

        attach_back(item);

        return true;
    }

    [[nodiscard]] bool insert_before(T& item, T& node) noexcept {
        assert(&item != &node);
        assert(item.prev == nullptr);
        assert(item.next == nullptr);

        if (&item == &node || item.prev != nullptr || item.next != nullptr)
            return false;

        link_before(item, node);

        attach();

        return true;
    }

    [[nodiscard]] bool insert_after(T& item, T& node) noexcept {
        assert(&item != &node);
        assert(item.prev == nullptr);
        assert(item.next == nullptr);

        if (&item == &node || item.prev != nullptr || item.next != nullptr)
            return false;

        link_after(item, node);

        attach();

        return true;
    }

    // ----------------------------------------------

    [[nodiscard]] bool move_front(T& item) noexcept {
        assert(contains(item));

        if (!contains(item))
            return false;

        if (front_ == &item)
            return true;

        unlink(item);

        link_front(item);

        return true;
    }

    [[nodiscard]] bool move_back(T& item) noexcept {
        assert(contains(item));

        if (!contains(item))
            return false;

        if (back_ == &item)
            return true;

        unlink(item);

        link_back(item);

        return true;
    }

    [[nodiscard]] bool move_before(T& item, T& node) noexcept {
        assert(contains(item));
        assert(contains(node));

        if (!contains(item) || !contains(node))
            return false;

        if (&item == &node || item.next == &node)
            return true;

        unlink(item);

        link_before(item, node);

        return true;
    }

    [[nodiscard]] bool move_after(T& item, T& node) noexcept {
        assert(contains(item));
        assert(contains(node));

        if (!contains(item) || !contains(node))
            return false;

        if (&item == &node || item.prev == &node)
            return true;

        unlink(item);

        link_after(item, node);

        return true;
    }

    [[nodiscard]] bool erase(T& item) noexcept {
        assert(contains(item));

        if (!contains(item))
            return false;

        detach(item);

        return true;
    }

    [[nodiscard]] T* next(T& item) noexcept {
        assert(contains(item));

        if (!contains(item))
            return nullptr;

        return item.next;
    }

    [[nodiscard]] const T* next(const T& item) const noexcept {
        assert(contains(item));

        if (!contains(item))
            return nullptr;

        return item.next;
    }

   private:
    [[nodiscard]] bool contains(const T& item) const noexcept {
        if (size_ == 0)
            return false;

        // An intrusive list without an owner pointer cannot generally determine membership
        // from an arbitrary node in O(1). The boundary checks below handle the common cases.
        if (front_ == &item || back_ == &item)
            return true;

        return item.prev != nullptr || item.next != nullptr;
    }

    void unlink(T& item) noexcept {
        if (item.prev != nullptr)
            item.prev->next = item.next;
        else
            front_ = item.next;

        if (item.next != nullptr)
            item.next->prev = item.prev;
        else
            back_ = item.prev;

        item.prev = nullptr;
        item.next = nullptr;
    }

    void link_front(T& item) noexcept {
        item.prev = nullptr;
        item.next = front_;

        if (front_ != nullptr)
            front_->prev = &item;
        else
            back_ = &item;

        front_ = &item;
    }

    void link_back(T& item) noexcept {
        item.prev = back_;
        item.next = nullptr;

        if (back_ != nullptr)
            back_->next = &item;
        else
            front_ = &item;

        back_ = &item;
    }

    void link_after(T& item, T& node) noexcept {
        item.prev = &node;
        item.next = node.next;

        if (node.next != nullptr)
            node.next->prev = &item;
        else
            back_ = &item;

        node.next = &item;
    }

    void link_before(T& item, T& node) noexcept {
        item.prev = node.prev;
        item.next = &node;

        if (node.prev != nullptr)
            node.prev->next = &item;
        else
            front_ = &item;

        node.prev = &item;
    }

    void attach() noexcept { ++size_; }

    void detach(T& item) noexcept {
        unlink(item);
        --size_;
    }

    void attach_front(T& item) noexcept {
        link_front(item);
        attach();
    }

    void attach_back(T& item) noexcept {
        link_back(item);
        attach();
    }


    T*    front_ = nullptr;
    T*    back_  = nullptr;
    usize size_  = 0;
};

// A lightweight intrusive doubly linked list that does not own its elements or allocate memory.
// Each element embeds a Node that stores pointers to the previous and next nodes, the owning list,
// and the associated element. The owner pointer allows the list to track whether an element is
// currently linked and which list owns it. This allows existing objects (e.g. stack-allocated
// mutex waiters) to be linked directly into the list without allocating separate nodes.
// Unlike std::list, it requires no dynamic allocation.
// Elements must remain alive and at stable addresses while linked, and list operations must
// be externally synchronized when accessed concurrently.
template<typename T>
class OwnedIntrusiveList final {
   public:
    struct Node {
        Node*               prev  = nullptr;
        Node*               next  = nullptr;
        OwnedIntrusiveList* owner = nullptr;
        T*                  data  = nullptr;
    };

    OwnedIntrusiveList() noexcept                                = default;
    OwnedIntrusiveList(const OwnedIntrusiveList&)                = delete;
    OwnedIntrusiveList& operator=(const OwnedIntrusiveList&)     = delete;
    OwnedIntrusiveList(OwnedIntrusiveList&&) noexcept            = delete;
    OwnedIntrusiveList& operator=(OwnedIntrusiveList&&) noexcept = delete;

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

    [[nodiscard]] bool push_front(Node& node) noexcept {
        assert(node.owner == nullptr);
        assert(node.data != nullptr);

        if (node.owner != nullptr || node.data == nullptr)
            return false;

        attach_front(node);

        return true;
    }

    [[nodiscard]] bool push_back(Node& node) noexcept {
        assert(node.owner == nullptr);
        assert(node.data != nullptr);

        if (node.owner != nullptr || node.data == nullptr)
            return false;

        attach_back(node);

        return true;
    }

    [[nodiscard]] bool move_front(Node& node) noexcept {
        assert(node.owner == this);

        if (node.owner != this)
            return false;

        if (front_ == &node)
            return true;

        unlink(node);
        attach_front(node);

        return true;
    }

    [[nodiscard]] bool move_back(Node& node) noexcept {
        assert(node.owner == this);

        if (node.owner != this)
            return false;

        if (back_ == &node)
            return true;

        unlink(node);
        attach_back(node);

        return true;
    }

    [[nodiscard]] bool erase(Node& node) noexcept {
        assert(node.owner == this);

        if (node.owner != this)
            return false;

        detach(node);

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
    void unlink(Node& node) noexcept {
        if (node.prev != nullptr)
            node.prev->next = node.next;
        else
            front_ = node.next;

        if (node.next != nullptr)
            node.next->prev = node.prev;
        else
            back_ = node.prev;

        node.prev = nullptr;
        node.next = nullptr;
    }

    void attach(Node& node) noexcept {
        node.owner = this;
        ++size_;
    }

    void detach(Node& node) noexcept {
        unlink(node);
        node.owner = nullptr;
        --size_;
    }

    void attach_front(Node& node) noexcept {
        node.prev = nullptr;
        node.next = front_;

        if (front_ != nullptr)
            front_->prev = &node;
        else
            back_ = &node;

        front_ = &node;

        attach(node);
    }

    void attach_back(Node& node) noexcept {
        node.prev = back_;
        node.next = nullptr;

        if (back_ != nullptr)
            back_->next = &node;
        else
            front_ = &node;

        back_ = &node;

        attach(node);
    }

    Node* front_ = nullptr;
    Node* back_  = nullptr;
    usize size_  = 0;
};

}  // namespace DON

#endif  // UTILITY_LIST_H_INCLUDED
