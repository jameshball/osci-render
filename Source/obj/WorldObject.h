#pragma once

#include <JuceHeader.h>
#include "Line3D.h"

// from https://www.keithlantz.net/2011/10/a-preliminary-wavefront-obj-loader-in-c/
struct vertex {
    std::vector<float> v;
    void normalize() {
        float magnitude = 0.0f;
        for (int i = 0; i < v.size(); i++)
            magnitude += pow(v[i], 2.0f);
        magnitude = sqrt(magnitude);
        for (int i = 0; i < v.size(); i++)
            v[i] /= magnitude;
    }
    vertex operator-(vertex v2) {
        vertex v3;
        if (v.size() != v2.v.size()) {
            v3.v.push_back(0.0f);
            v3.v.push_back(0.0f);
            v3.v.push_back(0.0f);
        }
        else {
            for (int i = 0; i < v.size(); i++)
                v3.v.push_back(v[i] - v2.v[i]);
        }
        return v3;
    }
    vertex cross(vertex v2) {
        vertex v3;
        if (v.size() != 3 || v2.v.size() != 3) {
            v3.v.push_back(0.0f);
            v3.v.push_back(0.0f);
            v3.v.push_back(0.0f);
        }
        else {
            v3.v.push_back(v[1] * v2.v[2] - v[2] * v2.v[1]);
            v3.v.push_back(v[2] * v2.v[0] - v[0] * v2.v[2]);
            v3.v.push_back(v[0] * v2.v[1] - v[1] * v2.v[0]);
        }
        return v3;
    }
};

struct face {
    std::vector<int> vertex;
    std::vector<int> texture;
    std::vector<int> normal;
};

class WorldObject {
public:
	WorldObject(juce::InputStream&);
    
	double rotateX = 0.0, rotateY = 0.0, rotateZ = 0.0;
    
    std::vector<Line3D> edges;
    std::vector<vertex> vertices;
private:
    std::vector<vertex> texcoords;
    std::vector<vertex> normals;
    std::vector<vertex> parameters;
    std::vector<face> faces;
};