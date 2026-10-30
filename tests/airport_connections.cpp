/*

Steps:

mkdir -p build

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -I./src tests/airport_connections.cpp -o build/airport_connections
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -DNDEBUG -I./src tests/airport_connections.cpp -o build/airport_connections

./build/airport_connections
*/

#include <iostream>
#include <limits>
#include <random>

#include "utility/graph.h"
#include "utility/types.h"

using namespace DON;


void airport_connections(const std::vector<std::string>&           airports,
                         const std::vector<Array<std::string, 2>>& routes,
                         const std::string&                        startAirport) noexcept {
    const usize n = airports.size();
    //const usize m = routes.size();

    if (n == 0)
    {
        std::cout << "No Input Airport List\n";
        return;
    }

    if (startAirport.empty())
    {
        std::cout << "No Starting Airport Provided\n";
        return;
    }

    Graph<std::string_view, GraphType::Directed> graph(n);

    for (auto& airport : airports)
    {
        graph.add_vertex(airport);
    }

    for (auto& route : routes)
    {
        graph.add_edge(route[0], route[1]);
    }

    std::cout << graph.to_string() << "\n\n";

    // Step 1: Find Strongly Connected Components (Kosaraju's Algorithm)
    StronglyConnectedComponents<std::string_view> scc(graph);

    const auto components = scc.find();

    for (usize i = 0; i < components.size(); ++i)
    {
        std::cout << "SCC " << i << ": {";

        const auto& component = components[i];

        for (usize j = 0; j < component.size(); ++j)
        {
            if (j != 0)
                std::cout << ", ";

            std::cout << component[j];
        }

        std::cout << "}\n";
    }

    // Step 2: Reduce graph to a Directed Acyclic Graph (DAG)
    auto [sccGraph, componentOf] = build_scc_graph(graph, components);

    std::cout << "\nSCC DAG:\n";

    for (const auto& [src, edges] : sccGraph.adjacencies())
    {
        std::cout << "SCC " << src << " -> ";

        for (const auto& [dst, weight] : edges)
        {
            (void) weight;
            std::cout << "SCC " << dst << " ";
        }

        std::cout << "\n";
    }
    std::cout << "\n";

    /*
    const auto reachable = find_reachable_sccs(startAirport, sccGraph, componentOf);

    std::cout << "\nReachable SCCs from " << startAirport << ":\n";

    for (const usize scc : reachable)
        std::cout << "SCC " << scc << '\n';
    */

    const auto reachable = calculate_reachability(sccGraph);

    for (usize i = 0; i < reachable.size(); ++i)
    {
        std::cout << "SCC " << i << " reaches: ";

        for (const usize scc : reachable[i])
            if (scc != i)
                std::cout << scc << " ";

        std::cout << "\n";
    }
    std::cout << "\n";

    const auto sources = find_source_sccs(sccGraph);

    std::cout << "Source SCCs: (" << sources.size() << ")\n";

    for (const usize scc : sources)
        std::cout << "SCC " << scc << "\n";
    std::cout << "\n";

    std::cout << "Suggested new connections:\n";

    for (const usize scc : sources)
    {
        const auto& airportsInSCC = components[scc];

        if (!airportsInSCC.empty())
            std::cout << startAirport << " -> " << airportsInSCC.front() << "\n";
    }
    std::cout << "\n";
}

int main() {

    std::ios_base::sync_with_stdio(false);
    std::cin.tie(nullptr);
    std::cout.tie(nullptr);

    /*
    usize n, m;
    std::cin >> n >> m;

    std::vector<std::string> airports(n);
    for (usize i = 0; i < n; ++i)
    {
        std::string airport;
        std::cin >> airport;
        airports[i] = airport;
    }

    std::vector<Array<std::string, 2>> routes(m);
    for (usize i = 0; i < m; ++i)
    {
        std::cin >> routes[i][0] >> routes[i][1];
        assert(routes[i][0] != routes[i][1]);
    }

    std::string startAirport;
    std::cin >> startAirport;
    */

    std::vector<std::string> airports{
      "BGI",  //
      "CDG",  //
      "DEL",  //
      "DOH",  //
      "DSM",  //
      "EWR",  //
      "EYW",  //
      "HND",  //
      "ICN",  //
      "JFK",  //
      "LGA",  //
      "LHR",  //
      "ORD",  //
      "SAN",  //
      "SFO",  //
      "SIN",  //
      "TLV",  //
      "BUD"   //
    };

    std::vector<Array<std::string, 2>> routes{
      {"DSM", "ORD"},  //
      {"ORD", "BGI"},  //
      {"BGI", "LGA"},  //
      {"SIN", "CDG"},  //
      {"CDG", "SIN"},  //
      {"CDG", "BUD"},  //
      {"DEL", "DOH"},  //
      {"DEL", "CDG"},  //
      {"TLV", "DEL"},  //
      {"EWR", "HND"},  //
      {"HND", "ICN"},  //
      {"HND", "JFK"},  //
      {"ICN", "JFK"},  //
      {"JFK", "LGA"},  //
      {"EYW", "LHR"},  //
      {"LHR", "SFO"},  //
      {"SFO", "SAN"},  //
      {"SFO", "DSM"},  //
      {"SAN", "EYW"},  //
      //{"LGA", "SFO"},  //
      //{"LGA", "TLV"},  //
      //{"LGA", "EWR"},  //
      //{"BUD", "SFO"},  //
      //{"DOH", "BUD"}   //
    };

    std::string startAirport = "LGA";

    const usize n = airports.size();
    const usize m = routes.size();

    std::cout << n << " " << m << "\n";

    for (auto& airport : airports)
    {
        std::cout << airport << "\n";
    }

    for (auto& route : routes)
    {
        std::cout << route[0] << " " << route[1] << "\n";
    }

    std::cout << startAirport << "\n\n";

    airport_connections(airports, routes, startAirport);


    return 0;
}
