/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./src tests/graph.cpp -o build/graph
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG -I./src tests/graph.cpp -o build/graph

./build/graph
*/

#include <cassert>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "utility/graph.h"

using namespace DON;

class ReferenceGraph final {
    usize                          n;
    std::vector<std::vector<bool>> edges;

   public:
    explicit ReferenceGraph(usize vertexCount) :
        n(vertexCount),
        edges(vertexCount, std::vector<bool>(vertexCount, false)) {}

    bool add_edge(usize from, usize to) {
        if (edges[from][to])
            return false;

        edges[from][to] = true;
        return true;
    }

    bool remove_edge(usize from, usize to) {
        if (!edges[from][to])
            return false;

        edges[from][to] = false;
        return true;
    }

    usize edge_count() const {
        usize count = 0;

        for (usize from = 0; from < n; ++from)
        {
            for (usize to = 0; to < n; ++to)
                count += edges[from][to] ? 1 : 0;
        }

        return count;
    }

    // Weak connectivity: treat every directed edge as undirected.
    bool connected(usize from, usize to) const {
        std::vector<bool>  visited(n, false);
        std::vector<usize> pending{from};

        visited[from] = true;

        for (usize i = 0; i < pending.size(); ++i)
        {
            const usize current = pending[i];

            for (usize next = 0; next < n; ++next)
            {
                if (!edges[current][next] && !edges[next][current])
                    continue;

                if (!visited[next])
                {
                    visited[next] = true;
                    pending.push_back(next);
                }
            }
        }

        return visited[to];
    }

    usize component_count() const {
        std::vector<bool> visited(n, false);
        usize             count = 0;

        for (usize start = 0; start < n; ++start)
        {
            if (visited[start])
                continue;

            ++count;
            visited[start] = true;

            std::vector<usize> pending;
            pending.push_back(start);

            for (usize i = 0; i < pending.size(); ++i)
            {
                const usize current = pending[i];

                for (usize next = 0; next < n; ++next)
                {
                    if (!edges[current][next] && !edges[next][current])
                        continue;

                    if (!visited[next])
                    {
                        visited[next] = true;
                        pending.push_back(next);
                    }
                }
            }
        }

        return count;
    }
};

int main() {
    // Directed graph: default and explicit weights.
    {
        Graph<std::string> graph(16);

        assert(graph.add_edge("A", "B"));
        assert(graph.add_edge("B", "C", 7));

        assert(*graph.get_weight("A", "B") == 0);
        assert(*graph.get_weight("B", "C") == 7);

        assert(!graph.add_edge("A", "B", 99));
        assert(*graph.get_weight("A", "B") == 0);

        assert(graph.set_weight("A", "B", 5));
        assert(*graph.get_weight("A", "B") == 5);

        assert(graph.vertex_count() == 3);
        assert(graph.edge_count() == 2);
        assert(graph.component_count() == 1);

        assert(graph.remove_edge("B", "C"));
        assert(graph.component_count() == 2);

        assert(graph.remove_vertex("A"));
        assert(graph.vertex_count() == 2);
        assert(graph.edge_count() == 0);
        assert(graph.component_count() == 2);
    }

    // Undirected graph: each logical edge counts once.
    {
        Graph<int, GraphType::Undirected> graph;

        assert(graph.add_edge(1, 2, 10));
        assert(graph.contains_edge(1, 2));
        assert(graph.contains_edge(2, 1));

        assert(*graph.get_weight(1, 2) == 10);
        assert(*graph.get_weight(2, 1) == 10);

        assert(graph.set_weight(1, 2, 20));
        assert(*graph.get_weight(2, 1) == 20);

        assert(!graph.add_edge(2, 1));
        assert(graph.edge_count() == 1);

        assert(graph.add_edge(2, 3));
        assert(graph.component_count() == 1);

        assert(graph.remove_edge(2, 3));
        assert(graph.component_count() == 2);
    }

    // Ordered containers.
    {
        Graph<std::string, GraphType::Directed, double, std::map> graph;

        assert(graph.add_edge("X", "Y"));
        assert(graph.add_edge("Y", "Z", 2.5));

        assert(*graph.get_weight("X", "Y") == 0.0);
        assert(*graph.get_weight("Y", "Z") == 2.5);
        assert(graph.component_count() == 1);
    }

    // Isolated vertices and self-loops.
    {
        Graph<int> graph;

        assert(graph.add_vertex(1));
        assert(graph.add_vertex(2));
        assert(graph.component_count() == 2);

        assert(graph.add_edge(1, 1));
        assert(graph.edge_count() == 1);
        assert(graph.component_count() == 2);

        assert(graph.remove_edge(1, 1));
        assert(graph.edge_count() == 0);
        assert(graph.component_count() == 2);
    }

    {
        Graph<std::string, GraphType::Directed, int> graph;

        graph.add_edge("A", "B", 10);
        graph.add_edge("A", "C", 20);
        graph.add_edge("B", "C", 30);

        std::cout << graph.to_string() << '\n';
    }

    std::cout << "All graph tests passed!\n";
}
