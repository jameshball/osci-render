#include <juce_core/juce_core.h>
#include <osci_file_import/osci_file_import.h>
#include "../modules/osci_file_import/third_party/chinese_postman/Graph.h"
#include "../modules/osci_file_import/third_party/chinese_postman/ParallelWork.h"

class ObjLoadingTests final : public juce::UnitTest {
public:
    ObjLoadingTests() : juce::UnitTest("OBJ loading", "OBJ") {}

    void runTest() override {
        beginTest("Large sparse graphs count undirected edges once without index overflow");
        std::list<std::pair<int, int>> edges{{0, 49999}, {49999, 0}, {49998, 49999}};
        Graph sparse(50000, edges);
        expectEquals(sparse.GetNumEdges(), 2);
        expectEquals(sparse.GetEdgeIndex(0, 49999), sparse.GetEdgeIndex(49999, 0));
        expect(sparse.GetEdge(0) == std::make_pair(0, 49999));
        expect(sparse.IsAdjacent(49999, 49998));
        expect(!sparse.IsAdjacent(0, 49998));

        beginTest("Complete graph lookup agrees with stored edges in both directions");
        Graph complete(10);
        expectEquals(complete.GetNumEdges(), 45);
        for (int i = 0; i < complete.GetNumEdges(); ++i) {
            const auto [u, v] = complete.GetEdge(i);
            expectEquals(complete.GetEdgeIndex(u, v), i);
            expectEquals(complete.GetEdgeIndex(v, u), i);
        }

        beginTest("Reusing a graph preserves reconstructed adjacency order");
        std::list<std::pair<int, int>> input{{0, 1}, {0, 2}, {1, 2}, {2, 3}};
        Graph reused(4, input);
        const std::vector<int> visitOrder{0, 2, 3, 1};
        std::list<std::pair<int, int>> rebuiltEdges;
        for (int u : visitOrder) {
            for (int v : reused.AdjList(u)) { rebuiltEdges.emplace_back(u, v); }
        }
        Graph rebuilt(4, rebuiltEdges);
        reused.OrderAdjacency(visitOrder);
        for (int u = 0; u < 4; ++u) { expect(reused.AdjList(u) == rebuilt.AdjList(u)); }

        beginTest("A single OBJ edge produces a finite closed out-and-back route");
        WorldObject object("v 0 0 0\nv 1 0 0\nl 1 2\n");
        expectEquals(int(object.edges.size()), 2);
        if (object.edges.size() == 2) {
            const auto& a = object.edges[0];
            const auto& b = object.edges[1];
            expect(std::isfinite(a.x1) && std::isfinite(a.x2));
            expect(a.x1 == b.x2 && a.x2 == b.x1 && a.x1 != a.x2);
        }

        beginTest("Nested work runs every job exactly once and remains usable after exceptions");
        ParallelWork work(4);
        std::vector<std::atomic<int>> hits(128);
        for (auto& hit : hits) { hit.store(0); }
        work.forEach(8, [&](size_t outer) {
            work.forEach(16, [&](size_t inner) { ++hits[outer * 16 + inner]; });
        });
        for (auto& hit : hits) { expectEquals(hit.load(), 1); }
        bool caught = false;
        try {
            work.forEach(16, [](size_t i) {
                if (i == 0) { throw std::runtime_error("expected"); }
            });
        } catch (const std::runtime_error&) { caught = true; }
        expect(caught);
        std::atomic<int> count{0};
        work.forEach(16, [&](size_t) { ++count; });
        expectEquals(count.load(), 16);
    }
};

static ObjLoadingTests objLoadingTests;
