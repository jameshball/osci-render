#include "WorldObject.h"

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
            vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            parameters.push_back(v);
        }
        else if (key == "vt") { // texture coordinate
            vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            texcoords.push_back(v);
        }
        else if (key == "vn") { // normal
            vertex v; float x;
            while (!stringstream.eof()) {
                stringstream >> x >> std::ws;
                v.v.push_back(x);
            }
            v.normalize();
            normals.push_back(v);
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

    // IMPORTANT TODO: get rid of duplicate edges here using an unordered_set
    // as there are wayyy too many edges being rendered causing poor performance

	for (auto& f : faces) {
		for (int i = 0; i < f.vertex.size(); i++) {
			double x1 = (vertices[f.vertex[i]].v[0] - x) / max;
			double y1 = (vertices[f.vertex[i]].v[1] - y) / max;
			double z1 = (vertices[f.vertex[i]].v[2] - z) / max;
			double x2 = (vertices[f.vertex[(i + 1) % f.vertex.size()]].v[0] - x) / max;
			double y2 = (vertices[f.vertex[(i + 1) % f.vertex.size()]].v[1] - y) / max;
			double z2 = (vertices[f.vertex[(i + 1) % f.vertex.size()]].v[2] - z) / max;
            
			edges.push_back(Line3D(x1, y1, z1, x2, y2, z2));
		}
	}
}
