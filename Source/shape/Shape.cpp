#include "Shape.h"

double Shape::totalLength(std::vector<std::unique_ptr<Shape>>& shapes) {
    double length = 0.0;
	for (auto& shape : shapes) {
		length += shape->length();
	}
	return length;
}
