#pragma once

template<typename Container, typename Key>
auto contains(Container const &c, Key const &k)-> bool {
    return c.find(k) != c.end();
}
