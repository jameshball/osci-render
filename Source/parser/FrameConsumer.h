#pragma once

#include <vector>
#include <memory>
#include "../shape/Shape.h"

class FrameConsumer {
public:
	virtual void addFrame(std::vector<std::unique_ptr<Shape>> frame, int fileIndex) = 0;
};