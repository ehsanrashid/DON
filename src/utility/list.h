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
// Each element must contain a `next` pointer, allowing existing objects to be linked directly
// into the list without allocating separate nodes. Elements must remain alive and at stable
// addresses while linked. List operations must be externally synchronized when accessed concurrently.
template<typename T>
class SinglyIntrusiveList final {
   public:
    using value_type    = T;
    using pointer       = T*;
    using const_pointer = const T*;

    [[nodiscard]] pointer front() noexcept { return head; }

    [[nodiscard]] const_pointer front() const noexcept { return head; }

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));

        return size() == 0;
    }

    void push_front(T& node) noexcept {
        node.next = head;
        head      = &node;

        ++size_;
    }

    // position must belong to this list; node must be detached.
    bool insert_after(T& node, T& pos) noexcept {
        assert(&pos != &node);
        assert(node.next == nullptr);

        node.next = pos.next;
        pos.next  = &node;
        ++size_;
        return true;
    }

    [[nodiscard]] pointer pop_front() noexcept {
        T* const node = head;

        if (node == nullptr)
            return node;

        head       = node->next;
        node->next = nullptr;

        --size_;
        return node;
    }

    [[nodiscard]] T* next(const T& node) noexcept { return node.next; }

    [[nodiscard]] const T* next(const T& node) const noexcept { return node.next; }

    bool erase(T& node) noexcept {
        T* previous = nullptr;
        T* current  = head;

        while (current != nullptr && current != &node)
        {
            previous = current;
            current  = current->next;
        }

        assert(current == &node);
        if (current == nullptr)
            return false;

        if (previous != nullptr)
            previous->next = node.next;
        else
            head = node.next;

        node.next = nullptr;
        --size_;
        return true;
    }

    void clear() noexcept {
        head  = nullptr;
        size_ = 0;
    }

   private:
    pointer head  = nullptr;
    usize   size_ = 0;
};

// A lightweight intrusive doubly linked list that does not own its elements or allocate memory.
// Each element stores pointers to the previous and next elements, allowing existing objects
// (e.g. stack-allocated mutex waiters) to be linked directly into the list without allocating
// separate nodes. Unlike std::list, it requires no dynamic allocation.
// Elements must remain alive and at stable addresses while linked, and list operations must
// be externally synchronized when accessed concurrently.
// Unlike OwnedDoublyIntrusiveList, it does not track list ownership for individual elements.
template<typename T>
class DoublyIntrusiveList final {
   public:
    DoublyIntrusiveList() noexcept                                 = default;
    DoublyIntrusiveList(const DoublyIntrusiveList&)                = delete;
    DoublyIntrusiveList& operator=(const DoublyIntrusiveList&)     = delete;
    DoublyIntrusiveList(DoublyIntrusiveList&&) noexcept            = delete;
    DoublyIntrusiveList& operator=(DoublyIntrusiveList&&) noexcept = delete;

    [[nodiscard]] T*       front() noexcept { return front_; }
    [[nodiscard]] const T* front() const noexcept { return front_; }

    [[nodiscard]] T*       back() noexcept { return back_; }
    [[nodiscard]] const T* back() const noexcept { return back_; }

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));
        assert((back() == nullptr) == (size() == 0));

        return size() == 0;
    }

    [[nodiscard]] T* next(T& node) noexcept {
        assert(is_linked(node));

        return node.next;
    }

    [[nodiscard]] const T* next(const T& node) const noexcept {
        assert(is_linked(node));

        return node.next;
    }

    [[nodiscard]] T* prev(T& node) noexcept {
        assert(is_linked(node));

        return node.prev;
    }

    [[nodiscard]] const T* prev(const T& node) const noexcept {
        assert(is_linked(node));

        return node.prev;
    }

    bool push_front(T& node) noexcept {
        assert(!is_linked(node));
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        if (node.prev != nullptr || node.next != nullptr)
            return false;

        link_front(node);
        ++size_;
        return true;
    }

    bool push_back(T& node) noexcept {
        assert(!is_linked(node));
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        if (node.prev != nullptr || node.next != nullptr)
            return false;

        link_back(node);
        ++size_;
        return true;
    }

    bool insert_before(T& node, T& pos) noexcept {
        assert(&node != &pos);
        assert(!is_linked(node));
        assert(is_linked(pos));
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        if (&node == &pos || node.prev != nullptr || node.next != nullptr)
            return false;

        link_before(node, pos);
        ++size_;
        return true;
    }

    bool insert_after(T& node, T& pos) noexcept {
        assert(&node != &pos);
        assert(!is_linked(node));
        assert(is_linked(pos));
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        if (&node == &pos || node.prev != nullptr || node.next != nullptr)
            return false;

        link_after(node, pos);
        ++size_;
        return true;
    }

    bool move_front(T& node) noexcept {
        assert(is_linked(node));

        if (front_ == &node)
            return true;

        unlink(node);
        link_front(node);
        return true;
    }

    bool move_back(T& node) noexcept {
        assert(is_linked(node));

        if (back_ == &node)
            return true;

        unlink(node);
        link_back(node);
        return true;
    }

    bool move_before(T& node, T& pos) noexcept {
        assert(is_linked(node));
        assert(is_linked(pos));

        if (&node == &pos || node.next == &pos)
            return true;

        unlink(node);
        link_before(node, pos);
        return true;
    }

    bool move_after(T& node, T& pos) noexcept {
        assert(is_linked(node));
        assert(is_linked(pos));

        if (&node == &pos || node.prev == &pos)
            return true;

        unlink(node);
        link_after(node, pos);
        return true;
    }

    bool erase(T& node) noexcept {
        assert(is_linked(node));

        unlink(node);
        --size_;
        return true;
    }

    T* pop_front() noexcept {
        if (front_ == nullptr)
            return nullptr;

        T* const node = front_;
        unlink(*node);
        --size_;
        return node;
    }

    T* pop_back() noexcept {
        if (back_ == nullptr)
            return nullptr;

        T* const node = back_;
        unlink(*node);
        --size_;
        return node;
    }

    void clear() noexcept {
        while (pop_back() != nullptr)
        {}
    }

    // O(1): Does not reset the links in individual nodes.
    void clear_fast() noexcept {
        front_ = nullptr;
        back_  = nullptr;
        size_  = 0;
    }

   private:
    [[nodiscard]] bool is_linked(const T& node) const noexcept {
        // An intrusive list without an owner pointer cannot generally determine membership
        // from an arbitrary node in O(1). The boundary checks below handle the common cases.
        return front_ == &node || back_ == &node || node.prev != nullptr || node.next != nullptr;
    }

    [[nodiscard]] bool contains(const T& node) const noexcept {
        if (empty())
            return false;

        for (const T* current = front_; current != nullptr; current = current->next)
        {
            if (current == &node)
                return true;
        }

        return false;
    }

    void unlink(T& node) noexcept {
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

    void link_front(T& node) noexcept {
        node.prev = nullptr;
        node.next = front_;

        if (front_ != nullptr)
            front_->prev = &node;
        else
            back_ = &node;

        front_ = &node;
    }

    void link_back(T& node) noexcept {
        node.prev = back_;
        node.next = nullptr;

        if (back_ != nullptr)
            back_->next = &node;
        else
            front_ = &node;

        back_ = &node;
    }

    void link_after(T& node, T& pos) noexcept {
        node.prev = &pos;
        node.next = pos.next;

        if (pos.next != nullptr)
            pos.next->prev = &node;
        else
            back_ = &node;

        pos.next = &node;
    }

    void link_before(T& node, T& pos) noexcept {
        node.prev = pos.prev;
        node.next = &pos;

        if (pos.prev != nullptr)
            pos.prev->next = &node;
        else
            front_ = &node;

        pos.prev = &node;
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
class OwnedDoublyIntrusiveList final {
   public:
    struct Node {
        explicit Node(T* dataPtr = nullptr) noexcept :
            data(dataPtr) {}

        Node*                     prev  = nullptr;
        Node*                     next  = nullptr;
        OwnedDoublyIntrusiveList* owner = nullptr;
        T*                        data  = nullptr;
    };

    OwnedDoublyIntrusiveList() noexcept                                      = default;
    OwnedDoublyIntrusiveList(const OwnedDoublyIntrusiveList&)                = delete;
    OwnedDoublyIntrusiveList& operator=(const OwnedDoublyIntrusiveList&)     = delete;
    OwnedDoublyIntrusiveList(OwnedDoublyIntrusiveList&&) noexcept            = delete;
    OwnedDoublyIntrusiveList& operator=(OwnedDoublyIntrusiveList&&) noexcept = delete;

    [[nodiscard]] T*       front() noexcept { return front_ != nullptr ? front_->data : nullptr; }
    [[nodiscard]] const T* front() const noexcept {
        return front_ != nullptr ? front_->data : nullptr;
    }

    [[nodiscard]] T*       back() noexcept { return back_ != nullptr ? back_->data : nullptr; }
    [[nodiscard]] const T* back() const noexcept {
        return back_ != nullptr ? back_->data : nullptr;
    }

    [[nodiscard]] usize size() const noexcept { return size_; }

    [[nodiscard]] bool empty() const noexcept {
        assert((front() == nullptr) == (size() == 0));
        assert((back() == nullptr) == (size() == 0));

        return size() == 0;
    }

    [[nodiscard]] T* next(T& item) noexcept {
        assert(item.node.owner == this);

        Node* const node = item.node.next;

        return node != nullptr ? node->data : nullptr;
    }
    [[nodiscard]] const T* next(const T& item) const noexcept {
        assert(item.node.owner == this);

        const Node* const node = item.node.next;

        return node != nullptr ? node->data : nullptr;
    }

    [[nodiscard]] T* prev(T& item) noexcept {
        assert(item.node.owner == this);

        Node* const node = item.node.prev;

        return node != nullptr ? node->data : nullptr;
    }
    [[nodiscard]] const T* prev(const T& item) const noexcept {
        assert(item.node.owner == this);

        const Node* const node = item.node.prev;

        return node != nullptr ? node->data : nullptr;
    }

    bool push_front(Node& node) noexcept {
        assert(node.owner == nullptr);
        assert(node.data != nullptr);
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        attach_front(node);

        return true;
    }

    bool push_back(Node& node) noexcept {
        assert(node.owner == nullptr);
        assert(node.data != nullptr);
        assert(node.prev == nullptr);
        assert(node.next == nullptr);

        attach_back(node);

        return true;
    }

    bool move_front(Node& node) noexcept {
        assert(node.owner == this);

        if (front_ == &node)
            return true;

        unlink(node);
        attach_front(node);

        return true;
    }

    bool move_back(Node& node) noexcept {
        assert(node.owner == this);

        if (back_ == &node)
            return true;

        unlink(node);
        attach_back(node);

        return true;
    }

    bool erase(Node& node) noexcept {
        assert(node.owner == this);

        detach(node);

        return true;
    }

    Node* pop_front() noexcept {
        if (front_ == nullptr)
            return nullptr;

        Node* const node = front_;

        detach(*node);

        return node;
    }

    Node* pop_back() noexcept {
        if (back_ == nullptr)
            return nullptr;

        Node* const node = back_;

        detach(*node);

        return node;
    }

    void clear() noexcept {
        while (pop_back() != nullptr)
        {}
    }

    // O(1): Does not reset the links in individual nodes.
    void clear_fast() noexcept {
        front_ = nullptr;
        back_  = nullptr;
        size_  = 0;
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
