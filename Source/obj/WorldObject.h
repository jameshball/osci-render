#pragma once

#include "../shape/Line.h"

class WorldObject {
public:
	WorldObject(const std::string&);

    std::vector<std::unique_ptr<Shape>> draw();
    
    std::vector<Line> edges;
    std::vector<float> vs;
    int numVertices;
};
