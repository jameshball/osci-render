#include "WorldObject.h"
#include "../chinese_postman/ChinesePostman.h"

struct pair_hash {
    inline std::size_t operator()(const std::pair<int, int>& v) const {
        return v.first * 31 + v.second;
    }
};

WorldObject::WorldObject(juce::InputStream& stream) {
    std::string key;
    while (!stream.isExhausted()) {
		auto line = stream.readNextLine();
        key = "";
        std::stringstream stringstream(line.toStdString());
        stringstream >> key >> std::ws;

        if (key == "v") { // vertex
            vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            vertices.push_back(v);
        }
        else if (key == "vp") { // parameter
            /*vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            parameters.push_back(v);*/
        }
        else if (key == "vt") { // texture coordinate
            /*vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            texcoords.push_back(v);*/
        }
        else if (key == "vn") { // normal
            /*vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            v.normalize();
            normals.push_back(v);*/
        }
        else if (key == "f") { // face
            face f; int v, t, n;
            while (!stringstream.eof()) {
                stringstream >> v >> std::ws;
                f.vertex.push_back(v - 1);
                if (stringstream.peek() == '/') {
                    stringstream.get();
                    if (stringstream.peek() == '/') {
                        stringstream.get();
                        stringstream >> n >> std::ws;
                        f.normal.push_back(n - 1);
                    }
                    else {
                        stringstream >> t >> std::ws;
                        f.texture.push_back(t - 1);
                        if (stringstream.peek() == '/') {
                            stringstream.get();
                            stringstream >> n >> std::ws;
                            f.normal.push_back(n - 1);
                        }
                    }
                }
            }
            faces.push_back(f);
        }
    }

    std::unordered_set<std::pair<int, int>, pair_hash> edge_set;

    for (auto& f : faces) {
        int num_vertices = f.vertex.size();
        for (int i = 0; i < num_vertices; i++) {
            int start = f.vertex[i];
            int end = f.vertex[(i + 1) % num_vertices];

            int first = start < end ? start : end;
            int second = start < end ? end : start;

            edge_set.insert(std::make_pair(first, second));
        }
    }

    std::list<std::pair<int, int>> edge_list;

    for (auto& edge : edge_set) {
        edge_list.push_back(edge);
    }

    Graph graph(vertices.size(), edge_list);
    pair<list<int>, double> solution = ChinesePostman(graph);
    list<int>& path = solution.first;

    double x = 0.0, y = 0.0, z = 0.0;
    double max = 0.0;
    for (auto& v : vertices) {
        x += v.v[0];
        y += v.v[1];
        z += v.v[2];
        if (std::abs(v.v[0]) > max) max = std::abs(v.v[0]);
        if (std::abs(v.v[1]) > max) max = std::abs(v.v[1]);
        if (std::abs(v.v[2]) > max) max = std::abs(v.v[2]);
    }
    x /= vertices.size();
    y /= vertices.size();
    z /= vertices.size();

    for (auto& v : vertices) {
        v.v[0] = (v.v[0] - x) / max;
        v.v[1] = (v.v[1] - y) / max;
        v.v[2] = (v.v[2] - z) / max;
    }

    int prevVertex = -1;
    for (auto& vertex : path) {
        if (prevVertex != -1) {
            double x1 = vertices[prevVertex].v[0];
            double y1 = vertices[prevVertex].v[1];
            double z1 = vertices[prevVertex].v[2];
            double x2 = vertices[vertex].v[0];
            double y2 = vertices[vertex].v[1];
            double z2 = vertices[vertex].v[2];

            edges.push_back(Line3D(x1, y1, z1, x2, y2, z2));
        }
        prevVertex = vertex;
    }
}
