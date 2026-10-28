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

#ifndef UTILITY_GRAPH_H_INCLUDED
#define UTILITY_GRAPH_H_INCLUDED

#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "../utility/disjoint_set.h"
#include "../utility/types.h"

namespace DON {

enum class GraphType {
    Directed,
    Undirected
};

template<typename Vertex,
         GraphType Type                         = GraphType::Directed,
         typename Weight                        = int,
         template<typename, typename> class Map = std::unordered_map>
class Graph final {
   public:
    using vertex_type          = Vertex;
    using weight_type          = Weight;
    using adjacency_type       = Map<Vertex, Weight>;
    using map_type             = Map<Vertex, adjacency_type>;
    using component_type       = std::vector<Vertex>;
    using components_type      = std::vector<component_type>;
    using component_index_type = Map<Vertex, usize>;

   private:
    template<typename T, typename = void>
    struct HasReserve: std::false_type {};

    template<typename T>
    struct HasReserve<T, std::void_t<decltype(std::declval<T&>().reserve(usize{}))>>
        : std::true_type {};

   public:
    Graph() = default;

    explicit Graph(usize capacity) {
        if constexpr (HasReserve<map_type>::value)
            adjacencies_.reserve(capacity);

        if constexpr (HasReserve<component_index_type>::value)
            componentIndices.reserve(capacity);

        components_.reserve(capacity);
    }

    // Vertex operations

    bool add_vertex(const Vertex& vertex) {
        if (!ensure_vertex(vertex))
            return false;

        add_component(vertex);
        return true;
    }

    bool remove_vertex(const Vertex& vertex) {
        auto victimItr = adjacencies_.find(vertex);

        if (victimItr == adjacencies_.end())
            return false;

        // Remove outgoing edges.
        edgeCount -= victimItr->second.size();

        // Remove incoming edges.
        for (auto itr = adjacencies_.begin(); itr != adjacencies_.end(); ++itr)
        {
            if (itr == victimItr)
                continue;

            if (itr->second.erase(vertex) != 0)
            {
                if constexpr (Type == GraphType::Directed)
                    --edgeCount;
            }
        }

        adjacencies_.erase(victimItr);
        rebuild_components();

        return true;
    }

    bool contains_vertex(const Vertex& vertex) const {
        return adjacencies_.find(vertex) != adjacencies_.end();
    }

    // Edge operations

    // Returns false if the edge already exists.
    // Missing vertices are added automatically.
    // The default weight is zero.
    bool add_edge(const Vertex& src, const Vertex& dst, const Weight& weight = Weight{0}) {
        const bool addedSrc = ensure_vertex(src);
        const bool addedDst = ensure_vertex(dst);

        auto srcItr = adjacencies_.find(src);
        auto dstItr = adjacencies_.find(dst);

        const bool isSelfLoop = srcItr == dstItr;

        if (srcItr->second.find(dst) != srcItr->second.end())
        {
            if (addedSrc || addedDst)
                rebuild_components();

            return false;
        }

        auto [edgeItr, inserted] = srcItr->second.try_emplace(dst, weight);

        if (!inserted)
        {
            if (addedSrc || addedDst)
                rebuild_components();

            return false;
        }

        if constexpr (Type == GraphType::Undirected)
        {
            if (!isSelfLoop)
            {
                const auto [reverseItr, reverseInserted] = dstItr->second.try_emplace(src, weight);

                (void) reverseItr;

                if (!reverseInserted)
                {
                    srcItr->second.erase(edgeItr);

                    if (addedSrc || addedDst)
                        rebuild_components();

                    return false;
                }
            }
        }

        ++edgeCount;
        rebuild_components();

        return true;
    }

    bool remove_edge(const Vertex& src, const Vertex& dst) {
        auto srcItr = adjacencies_.find(src);

        if (srcItr == adjacencies_.end())
            return false;

        auto dstItr  = adjacencies_.find(dst);
        auto edgeItr = srcItr->second.find(dst);

        if (edgeItr == srcItr->second.end())
            return false;

        if constexpr (Type == GraphType::Undirected)
        {
            if (dstItr == adjacencies_.end())
                return false;

            if (dstItr != srcItr && dstItr->second.find(src) == dstItr->second.end())
                return false;

            if (dstItr != srcItr)
                dstItr->second.erase(src);
        }

        srcItr->second.erase(edgeItr);

        --edgeCount;
        rebuild_components();

        return true;
    }

    bool contains_edge(const Vertex& src, const Vertex& dst) const {
        auto srcItr = adjacencies_.find(src);

        return srcItr != adjacencies_.end()  //
            && srcItr->second.find(dst) != srcItr->second.end();
    }

    // Returns nullptr if the edge does not exist.
    const Weight* get_weight(const Vertex& src, const Vertex& dst) const {
        auto srcItr = adjacencies_.find(src);

        if (srcItr == adjacencies_.end())
            return nullptr;

        auto edgeItr = srcItr->second.find(dst);

        return edgeItr == srcItr->second.end() ? nullptr : &edgeItr->second;
    }

    // Updates an existing edge's weight.
    bool set_weight(const Vertex& src, const Vertex& dst, const Weight& weight) {
        auto srcItr = adjacencies_.find(src);

        if (srcItr == adjacencies_.end())
            return false;

        auto edgeItr = srcItr->second.find(dst);

        if (edgeItr == srcItr->second.end())
            return false;

        if constexpr (Type == GraphType::Undirected)
        {
            auto dstItr = adjacencies_.find(dst);

            if (dstItr == adjacencies_.end())
                return false;

            if (dstItr != srcItr)
            {
                auto reverseItr = dstItr->second.find(src);

                if (reverseItr == dstItr->second.end())
                    return false;

                edgeItr->second    = weight;
                reverseItr->second = weight;
            }
            else
            {
                edgeItr->second = weight;
            }
        }
        else
        {
            edgeItr->second = weight;
        }

        return true;
    }

    // Connectivity

    // Directed graphs use weak connectivity: edge direction is ignored.
    bool connected(const Vertex& src, const Vertex& dst) const { return connected_(src, dst); }

    // Counts and access

    // Returns the authoritative adjacency collection.
    const map_type& adjacencies() const noexcept { return adjacencies_; }

    // Returns the adjacency collection for a vertex, or nullptr if absent.
    const adjacency_type* adjacency_of(const Vertex& vertex) const {
        auto itr = adjacencies_.find(vertex);

        return itr == adjacencies_.end() ? nullptr : &itr->second;
    }

    // Returns the number of vertices.
    usize vertex_count() const noexcept { return adjacencies_.size(); }

    // Returns the number of edges. Undirected edges are counted once.
    usize edge_count() const noexcept { return edgeCount; }

    // Returns the authoritative component collection.
    // Each element contains all vertices belonging to one weakly connected component.
    const components_type& components() const noexcept { return components_; }

    // Returns the number of weakly connected components.
    usize component_count() const noexcept { return components_.size(); }

    // Returns the mapping from each vertex to its component index.
    // The index refers to an element in components().
    const component_index_type& component_index() const noexcept { return componentIndices; }

    // Returns the component index for a vertex, or nullptr if absent.
    // The returned pointer is invalidated by topology changes.
    const usize* component_index_of(const Vertex& vertex) const {
        auto itr = componentIndices.find(vertex);

        return itr == componentIndices.end() ? nullptr : &itr->second;
    }

    // Returns the size of the component containing vertex.
    //
    // O(1) average with std::unordered_map.
    // O(log V) with std::map.
    usize component_size_of(const Vertex& vertex) const {
        auto itr = componentIndices.find(vertex);

        if (itr == componentIndices.end())
            return 0;

        return components_[itr->second].size();
    }

    bool empty() const noexcept { return adjacencies_.empty(); }

    // Returns the adjacency collection for a vertex, or nullptr if absent.
    const adjacency_type* neighbors(const Vertex& vertex) const { return adjacency_of(vertex); }

    void clear() noexcept {
        adjacencies_.clear();
        components_.clear();
        componentIndices.clear();
        edgeCount = 0;
    }

    std::string to_string() const {
        std::ostringstream oss;

        oss << "[\n"
            << "    type: " << (Type == GraphType::Directed ? "Directed" : "Undirected") << "\n"
            << "    vertices: " << vertex_count() << "\n"
            << "    edges: " << edge_count() << "\n"
            << "    components: " << component_count() << "\n"
            << "    component sizes:\n";

        for (const auto& [vertex, index] : component_index())
        {
            oss << "        " << vertex << ": " << components_[index].size() << "\n";
        }

        oss << "    adjacencies:\n";

        for (const auto& [vertex, edges] : adjacencies())
        {
            oss << "        " << vertex << " -> [";

            bool first = true;

            for (const auto& [neighbor, weight] : edges)
            {
                if (!first)
                    oss << ", ";

                oss << neighbor << " (" << weight << ")";
                first = false;
            }

            oss << "]\n";
        }

        oss << "]";

        return oss.str();
    }

   private:
    // Inserts a vertex without rebuilding component data.
    // Used internally to batch topology changes.
    bool ensure_vertex(const Vertex& vertex) { return adjacencies_.try_emplace(vertex).second; }

    void add_component(const Vertex& vertex) {
        const usize index = components_.size();

        components_.emplace_back(1, vertex);
        componentIndices.try_emplace(vertex, index);
    }

    // Rebuild weakly connected components using DSU.
    //
    // For directed graphs, every directed edge A -> B is treated as
    // an undirected connection A <-> B.
    //
    // Complexity:
    //     O((V + E) * alpha(V))
    //
    // which is effectively O(V + E).

    void rebuild_components() {
        components_.clear();
        componentIndices.clear();

        const usize vertexCount = adjacencies_.size();

        if (vertexCount == 0)
            return;

        if constexpr (HasReserve<component_index_type>::value)
            componentIndices.reserve(vertexCount);

        // Assign each vertex a compact integer index.
        Map<Vertex, usize> vertexIndices;

        if constexpr (HasReserve<Map<Vertex, usize>>::value)
            vertexIndices.reserve(vertexCount);

        usize index = 0;

        for (const auto& [vertex, edges] : adjacencies_)
        {
            (void) edges;
            vertexIndices.try_emplace(vertex, index++);
        }

        DisjointSet disjointSet(vertexCount);

        // Build weakly connected components by ignoring edge direction.
        for (const auto& [vertex, edges] : adjacencies_)
        {
            const usize srcIndex = vertexIndices.find(vertex)->second;

            for (const auto& [neighbor, weight] : edges)
            {
                (void) weight;

                const usize dstIndex = vertexIndices.find(neighbor)->second;
                disjointSet.unite(srcIndex, dstIndex);
            }
        }

        const usize componentCount = disjointSet.components();

        components_.reserve(componentCount);

        // Map DSU roots to compact component indices.
        Map<usize, usize> rootIndices;

        if constexpr (HasReserve<Map<usize, usize>>::value)
            rootIndices.reserve(componentCount);

        // Group vertices and build vertex -> component index simultaneously.
        for (const auto& [vertex, vertexIndex] : vertexIndices)
        {
            const usize root = disjointSet.find(vertexIndex);

            auto [rootItr, inserted] = rootIndices.try_emplace(root, components_.size());

            if (inserted)
            {
                components_.emplace_back();

                // Reserve the exact number of vertices in this component.
                components_.back().reserve(disjointSet.size(root));
            }

            const usize componentIndex = rootItr->second;

            components_[componentIndex].push_back(vertex);
            componentIndices.try_emplace(vertex, componentIndex);
        }
    }

    bool connected_(const Vertex& src, const Vertex& dst) const {
        auto srcItr = componentIndices.find(src);
        auto dstItr = componentIndices.find(dst);

        return srcItr != componentIndices.end() && dstItr != componentIndices.end()
            && srcItr->second == dstItr->second;
    }

    map_type             adjacencies_;
    components_type      components_;
    component_index_type componentIndices;
    usize                edgeCount = 0;
};

}  // namespace DON

#endif  // UTILITY_GRAPH_H_INCLUDED
