/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -I./src \
    tests/list.cpp \
    -o build/list
g++ -std=c++17 -O2 -I./src \
    tests/list.cpp \
    -o build/list
g++ -std=c++17 -O2 -I./src `
    tests/list.cpp `
    -o build/list

./build/list
*/

#include <cassert>
#include <cstddef>
#include <iostream>
#include <vector>

#include "utility/list.h"

using namespace DON;

// Example intrusive element.
struct Item {
    int   id;
    Item* prev = nullptr;
    Item* next = nullptr;

    explicit Item(int value) noexcept :
        id(value) {}
};

using List = IntrusiveList<Item>;

void check_list(const List& list, const std::vector<const Item*>& expected) {
    assert(list.size() == expected.size());
    assert(list.empty() == expected.empty());

    if (expected.empty())
    {
        assert(list.front() == nullptr);
        assert(list.back() == nullptr);
        return;
    }

    assert(list.front() == expected.front());
    assert(list.back() == expected.back());

    const Item* item = list.front();

    for (const Item* expectedItem : expected)
    {
        assert(item == expectedItem);
        assert(item != nullptr);
        item = item->next;
    }

    assert(item == nullptr);

    // Verify backward links and traversal.
    item = list.back();

    for (auto itr = expected.rbegin(); itr != expected.rend(); ++itr)
    {
        assert(item == *itr);
        assert(item != nullptr);
        item = item->prev;
    }

    assert(item == nullptr);
}


#define CHECK(condition) \
    do \
    { \
        if (!(condition)) \
        { \
            std::cerr << "Test failed: " #condition << " at " << __FILE__ << ':' << __LINE__ \
                      << '\n'; \
            return 1; \
        } \
    } while (false)

int main() {
    Item a{1};
    Item b{2};
    Item c{3};
    Item d{4};
    Item e{5};

    List list;

    // Empty list.
    check_list(list, {});

    // Push front/back.
    CHECK(list.push_front(a));
    check_list(list, {&a});

    CHECK(list.push_back(b));
    check_list(list, {&a, &b});

    CHECK(list.push_front(c));
    check_list(list, {&c, &a, &b});

    // Reject inserting an already-linked element.
    // Uncomment only for release builds with NDEBUG.
    // CHECK(!list.push_front(a));
    // CHECK(!list.push_back(b));
    check_list(list, {&c, &a, &b});

    // Insert before/after.
    CHECK(list.insert_before(d, a));
    check_list(list, {&c, &d, &a, &b});

    CHECK(list.insert_after(e, a));
    check_list(list, {&c, &d, &a, &e, &b});

    // Reject self-insertion.
    // Uncomment only for release builds with NDEBUG.
    // CHECK(!list.insert_before(a, a));
    // CHECK(!list.insert_after(a, a));
    check_list(list, {&c, &d, &a, &e, &b});

    // Move to front/back.
    CHECK(list.move_front(e));
    check_list(list, {&e, &c, &d, &a, &b});

    CHECK(list.move_back(c));
    check_list(list, {&e, &d, &a, &b, &c});

    // Move before/after.
    CHECK(list.move_before(c, d));
    check_list(list, {&e, &c, &d, &a, &b});

    CHECK(list.move_after(e, a));
    check_list(list, {&c, &d, &a, &e, &b});

    // No-op moves: already at the requested position.
    CHECK(list.move_front(c));
    check_list(list, {&c, &d, &a, &e, &b});

    CHECK(list.move_before(d, a));
    check_list(list, {&c, &d, &a, &e, &b});

    CHECK(list.move_after(e, a));
    check_list(list, {&c, &d, &a, &e, &b});

    CHECK(list.move_back(b));
    check_list(list, {&c, &d, &a, &e, &b});

    // Erase front, middle, and back.
    CHECK(list.erase(c));
    check_list(list, {&d, &a, &e, &b});

    CHECK(list.erase(a));
    check_list(list, {&d, &e, &b});

    CHECK(list.erase(b));
    check_list(list, {&d, &e});

    // Erase until empty.
    CHECK(list.erase(d));
    check_list(list, {&e});

    CHECK(list.erase(e));
    check_list(list, {});

    // Removed elements have cleared links and can be reused.
    CHECK(a.prev == nullptr && a.next == nullptr);
    CHECK(b.prev == nullptr && b.next == nullptr);
    CHECK(c.prev == nullptr && c.next == nullptr);
    CHECK(d.prev == nullptr && d.next == nullptr);
    CHECK(e.prev == nullptr && e.next == nullptr);

    CHECK(list.push_back(a));
    CHECK(list.push_front(b));
    check_list(list, {&b, &a});

    CHECK(list.erase(b));
    CHECK(list.erase(a));
    check_list(list, {});

    std::cout << "All IntrusiveList tests passed.\n";
    return 0;
}
