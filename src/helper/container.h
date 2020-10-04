#pragma once

template<typename Container, typename Key>
auto contains(Container const &container, Key const &key)-> bool {
    return container.find(key) != container.end();
}
