#pragma once

#include <JuceHeader.h>

class WorldObject {
public:
	WorldObject(const std::string&);

    std::vector<std::unique_ptr<osci::Shape>> draw();
    
    std::vector<osci::Line> edges;
    std::vector<float> vs;
    int numVertices;
};
