#include "Shape.h"
#include "Line.h"
#include "Point.h"

double Shape::totalLength(std::vector<std::unique_ptr<Shape>>& shapes) {
    double length = 0.0;
	for (auto& shape : shapes) {
		length += shape->length();
	}
	return length;
}

void Shape::normalize(std::vector<std::unique_ptr<Shape>>& shapes, double width, double height) {
    double maxDim = std::max(width, height);
    
	for (auto& shape : shapes) {
		shape->scale(2.0 / maxDim, -2.0 / maxDim);
		shape->translate(-1.0, 1.0);
	}

	removeOutOfBounds(shapes);
}

void Shape::normalize(std::vector<std::unique_ptr<Shape>>& shapes) {
    double oldHeight = height(shapes);
	double oldWidth = width(shapes);
	double maxDim = std::max(oldHeight, oldWidth);

	for (auto& shape : shapes) {
		shape->scale(2.0 / maxDim, -2.0 / maxDim);
	}

	Point max = maxVector(shapes);
	double newHeight = height(shapes);

	for (auto& shape : shapes) {
		shape->translate(-1.0, -max.y + newHeight / 2.0);
	}
}

double Shape::height(std::vector<std::unique_ptr<Shape>>& shapes) {
	double maxY = std::numeric_limits<double>::min();
	double minY = std::numeric_limits<double>::max();

    Point vectors[4];

    for (auto& shape : shapes) {
        for (int i = 0; i < 4; i++) {
            vectors[i] = shape->nextVector(i * 1.0 / 4.0);
        }

        for (auto& vector : vectors) {
            maxY = std::max(maxY, vector.y);
            minY = std::min(minY, vector.y);
        }
    }

    return std::abs(maxY - minY);
}

double Shape::width(std::vector<std::unique_ptr<Shape>>& shapes) {
    double maxX = std::numeric_limits<double>::min();
    double minX = std::numeric_limits<double>::max();

    Point vectors[4];

    for (auto& shape : shapes) {
        for (int i = 0; i < 4; i++) {
            vectors[i] = shape->nextVector(i * 1.0 / 4.0);
        }

        for (auto& vector : vectors) {
            maxX = std::max(maxX, vector.x);
            minX = std::min(minX, vector.x);
        }
    }

    return std::abs(maxX - minX);
}

Point Shape::maxVector(std::vector<std::unique_ptr<Shape>>& shapes) {
    double maxX = std::numeric_limits<double>::min();
    double maxY = std::numeric_limits<double>::min();

    for (auto& shape : shapes) {
        Point startVector = shape->nextVector(0);
        Point endVector = shape->nextVector(1);

        double x = std::max(startVector.x, endVector.x);
        double y = std::max(startVector.y, endVector.y);

        maxX = std::max(x, maxX);
        maxY = std::max(y, maxY);
    }

	return Point(maxX, maxY);
}

void Shape::removeOutOfBounds(std::vector<std::unique_ptr<Shape>>& shapes) {
    std::vector<int> toRemove;

    for (int i = 0; i < shapes.size(); i++) {
		Point start = shapes[i]->nextVector(0);
		Point end = shapes[i]->nextVector(1);
        bool keep = false;

        if ((start.x < 1 && start.x > -1) || (start.y < 1 && start.y > -1)) {
            if ((end.x < 1 && end.x > -1) || (end.y < 1 && end.y > -1)) {
				if (shapes[i]->type() == "Line") {
                    Point newStart(std::min(std::max(start.x, -1.0), 1.0), std::min(std::max(start.y, -1.0), 1.0));
                    Point newEnd(std::min(std::max(end.x, -1.0), 1.0), std::min(std::max(end.y, -1.0), 1.0));
					shapes[i] = std::make_unique<Line>(newStart.x, newStart.y, newEnd.x, newEnd.y);
                }
                keep = true;
            }
        }

        if (!keep) {
            toRemove.push_back(i);
        }
    }

    for (int i = toRemove.size() - 1; i >= 0; i--) {
        shapes.erase(shapes.begin() + toRemove[i]);
    }
}
