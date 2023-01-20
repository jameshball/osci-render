#pragma once

#include "Line3D.h"

class WorldObject {
public:
	WorldObject(std::string);
    
	double rotateX = 0.0, rotateY = 0.0, rotateZ = 0.0;
    
    std::vector<Line3D> edges;
    std::vector<float> vs;
    int numVertices;
private:
};