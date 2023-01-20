#include "WorldObject.h"
#include "../chinese_postman/ChinesePostman.h"
#include "tiny_obj_loader.h"

struct pair_hash {
    inline std::size_t operator()(const std::pair<int, int>& v) const {
        return v.first * 31 + v.second;
    }
};

std::vector<std::vector<int>> ConnectedComponents(const Graph& G) {
    std::vector<std::vector<int>> components;
    std::vector<bool> visited(G.GetNumVertices(), false);
    list<int> L;

    for (int i = 0; i < visited.size(); i++) {
        // if condition should only be true for the first element in
        // a new connected component
        if (!visited[i]) {
            components.emplace_back();
            // Breadth First Search
            L.push_back(i);
            while (not L.empty()) {
                int u = L.back();
                L.pop_back();
                if (visited[u]) continue;

                visited[u] = true;
                components.back().push_back(u);

                for (int v : G.AdjList(u)) {
                    L.push_back(v);
                }
            }
        }
    }

    return components;
}

WorldObject::WorldObject(juce::InputStream& stream) {
    tinyobj::ObjReaderConfig reader_config;
    tinyobj::ObjReader reader;

    reader.ParseFromString(stream.readEntireStreamAsString().toStdString(), "", reader_config);

    vs = reader.GetAttrib().vertices;
	numVertices = vs.size() / 3;

    double x = 0.0, y = 0.0, z = 0.0;
    double max = 0.0;

    for (int i = 0; i < numVertices; i++) {
        x += vs[i * 3];
        y += vs[i * 3 + 1];
        z += vs[i * 3 + 2];
        if (std::abs(vs[i * 3]) > max) {
            max = std::abs(vs[i * 3]);
        }
        if (std::abs(vs[i * 3 + 1]) > max) {
            max = std::abs(vs[i * 3 + 1]);
        }
        if (std::abs(vs[i * 3 + 2]) > max) {
            max = std::abs(vs[i * 3 + 2]);
        }
    }
    x /= numVertices;
    y /= numVertices;
    z /= numVertices;

    for (int i = 0; i < numVertices; i++) {
        vs[i * 3] = (vs[i * 3] - x) / max;
        vs[i * 3 + 1] = (vs[i * 3 + 1] - y) / max;
        vs[i * 3 + 2] = (vs[i * 3 + 2] - z) / max;
    }

    std::vector<tinyobj::shape_t> shapes = reader.GetShapes();
    
    std::unordered_set<std::pair<int, int>, pair_hash> edge_set;
	for (auto& shape : shapes) {
        int i = 0;
        int face = 0;
        while (i < shape.mesh.indices.size()) {
            int prevVertex = -1;
			for (int j = 0; j < shape.mesh.num_face_vertices[face]; j++) {
				int vertex = shape.mesh.indices[i].vertex_index;
				if (prevVertex != -1) {
					edge_set.insert(std::make_pair(prevVertex, vertex));
				}
				prevVertex = vertex;
				i++;
			}
            face++;
        }
	}

    std::list<std::pair<int, int>> edge_list;

    for (auto& edge : edge_set) {
        edge_list.push_back(edge);
    }

    Graph graph(numVertices, edge_list);

    std::vector<std::vector<int>> connected_components = ConnectedComponents(graph);

    for (auto& connected_component : connected_components) {
        std::vector<bool> present_vertices(graph.GetNumVertices(), false);

        for (int vertex : connected_component) {
            present_vertices[vertex] = true;
        }

        std::unordered_map<int, int> obj_to_graph_vertex;
        std::unordered_map<int, int> graph_to_obj_vertex;

        int count = 0;
        for (int i = 0; i < graph.GetNumVertices(); i++) {
            if (present_vertices[i]) {
                obj_to_graph_vertex[i] = i - count;
                graph_to_obj_vertex[i - count] = i;
            } else {
                count++;
            }
        }

        std::list<std::pair<int, int>> sub_edge_list;

        for (int obj_start : connected_component) {
            for (int obj_end : graph.AdjList(obj_start)) {
                int graph_start = obj_to_graph_vertex[obj_start];
                int graph_end = obj_to_graph_vertex[obj_end];
                sub_edge_list.push_back(std::make_pair(graph_start, graph_end));
            }
        }

        Graph subgraph(connected_component.size(), sub_edge_list);
        pair<list<int>, double> solution = ChinesePostman(subgraph);
        list<int>& path = solution.first;

        int prevVertex = -1;
        for (auto& graph_vertex : path) {
            int vertex = graph_to_obj_vertex[graph_vertex];
            if (prevVertex != -1) {
                double x1 = vs[prevVertex * 3];
                double y1 = vs[prevVertex * 3 + 1];
                double z1 = vs[prevVertex * 3 + 2];
                double x2 = vs[vertex * 3];
                double y2 = vs[vertex * 3 + 1];
                double z2 = vs[vertex * 3 + 2];

                edges.push_back(Line3D(x1, y1, z1, x2, y2, z2));
            }
            prevVertex = vertex;
        }
    }
}
