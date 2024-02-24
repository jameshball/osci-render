#include "WorldObject.h"
#include "../chinese_postman/ChinesePostman.h"
#include "tiny_obj_loader.h"
#include "../MathUtil.h"
#include <unordered_set>

struct pair_hash {
    inline std::size_t operator()(const std::pair<int, int>& v) const {
        return v.first * 31 + v.second;
    }
};

//
// returns all vertex indices in all connected sub-components of the graph
// TODO: move this to a graph class
//
std::vector<std::vector<int>> ConnectedComponents(Graph& G) {
    std::vector<std::vector<int>> components;
    std::vector<bool> visited(G.GetNumVertices(), false);
    std::list<int> L;

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

WorldObject::WorldObject(const std::string& obj_string) {
    tinyobj::ObjReaderConfig reader_config;
    reader_config.triangulate = false;
    reader_config.vertex_color = false;
    tinyobj::ObjReader reader;

    reader.ParseFromString(obj_string, "", reader_config);

    vs = reader.GetAttrib().vertices;
	numVertices = vs.size() / 3;

    //
    // normalising object vertices
    //
    double x = 0.0, y = 0.0, z = 0.0;
    for (int i = 0; i < numVertices; i++) {
        x += vs[i * 3];
        y += vs[i * 3 + 1];
        z += vs[i * 3 + 2];
    }
    x /= numVertices;
    y /= numVertices;
    z /= numVertices;

    float max = 0.0;
    for (int i = 0; i < numVertices; i++) {
        float newX = vs[i * 3] - x;
        float newY = vs[i * 3 + 1] - y;
        float newZ = vs[i * 3 + 2] - z;

        float det = newX * newX + newY * newY + newZ * newZ;
        max = det > max ? det : max;
        
        vs[i * 3] = newX;
        vs[i * 3 + 1] = newY;
        vs[i * 3 + 2] = newZ;
    }

    max = std::sqrt(max);
    
    // scaling down so that it's slightly smaller
    max = 1.2 * max;

    for (int i = 0; i < vs.size(); i++) {
        vs[i] /= max;
    }

    //
    // getting edges from obj file
    // 
    std::vector<tinyobj::shape_t> shapes = reader.GetShapes();
    std::unordered_set<std::pair<int, int>, pair_hash> edge_set;

	for (auto& shape : shapes) {
        int i = 0;
        int face = 0;
        while (i < shape.mesh.indices.size()) {
            int prevVertex = -1;
            int num_face_vertices = shape.mesh.num_face_vertices[face];
            int firstVertex;
            int lastVertex;
            // num_face_vertices stores the number of vertices per face, used to
            // iterate through mesh.indices and know when a face starts/ends
            for (int j = 0; j < num_face_vertices; j++) {
                int vertex = shape.mesh.indices[i].vertex_index;
                if (j == 0) {
                    firstVertex = vertex;
                }
                if (j == num_face_vertices - 1) {
                    lastVertex = vertex;
                }
				if (prevVertex != -1) {
					int first = std::min(prevVertex, vertex);
					int last = std::max(prevVertex, vertex);
					edge_set.insert(std::make_pair(first, last));
				}
				prevVertex = vertex;
				i++;
			}
            int first = std::min(firstVertex, lastVertex);
            int last = std::max(firstVertex, lastVertex);
            edge_set.insert(std::make_pair(first, last));
            face++;
        }
	}

    std::list<std::pair<int, int>> edge_list;

    for (auto& edge : edge_set) {
        edge_list.push_back(edge);
    }

    Graph graph(numVertices, edge_list);
    std::vector<std::vector<int>> connected_components = ConnectedComponents(graph);

    // perform chinese postman on all connected sub-components of graph
    // TODO: move this to separate graph-related file
    for (auto& connected_component : connected_components) {
        // TODO: make this parallel: https://stackoverflow.com/questions/36246300/parallel-loops-in-c
		// TODO: check the number of edges in the subgraph to make sure it's not too large compared to java version

        //
        // get a mapping to graph vertices that doesn't skip over
		// any numbers, allowing the Graph class to be used
        // 
		// we also need a mapping back to the obj vertices so that
        // we can construct the path at the end
        //
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

        // generate all edges in sub-component using the vertex
        // maps and parent Graph's adjacency list
        std::list<std::pair<int, int>> sub_edge_list;

        for (int obj_start : connected_component) {
            for (int obj_end : graph.AdjList(obj_start)) {
                int graph_start = obj_to_graph_vertex[obj_start];
                int graph_end = obj_to_graph_vertex[obj_end];
                sub_edge_list.push_back(std::make_pair(graph_start, graph_end));
            }
        }

        Graph subgraph(connected_component.size(), sub_edge_list);

        std::vector<double> cost(subgraph.GetNumEdges());
		for (auto& edge : sub_edge_list) {
            int obj_start = graph_to_obj_vertex[edge.first];
            int obj_end = graph_to_obj_vertex[edge.second];
			double deltax = vs[3 * obj_start] - vs[3 * obj_end];
			double deltay = vs[3 * obj_start + 1] - vs[3 * obj_end + 1];
			double deltaz = vs[3 * obj_start + 2] - vs[3 * obj_end + 2];
			double c = std::sqrt(deltax * deltax + deltay * deltay + deltaz * deltaz);
            cost[subgraph.GetEdgeIndex(edge.first, edge.second)] = c;
		}

        pair<list<int>, double> solution = ChinesePostman(subgraph, cost);
        list<int>& path = solution.first;

        // traverse CP solution, converting back to obj vertices
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

                edges.push_back(Line(x1, y1, z1, x2, y2, z2));
            }
            prevVertex = vertex;
        }
    }
}

std::vector<std::unique_ptr<Shape>> WorldObject::draw() {
    std::vector<std::unique_ptr<Shape>> shapes;

    for (auto& edge : edges) {
        shapes.push_back(edge.clone());
    }
    return shapes;
}
