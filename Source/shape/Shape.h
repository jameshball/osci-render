#pragma once

#include <cmath>
#include <cstdlib>
#include <vector>
#include <memory>
#include <string>

class Point;
class Shape {
public:
	virtual Point nextVector(double drawingProgress) = 0;
	virtual void scale(double x, double y) = 0;
	virtual void translate(double x, double y) = 0;
	virtual double length() = 0;
	virtual std::unique_ptr<Shape> clone() = 0;
	virtual std::string type() = 0;

	static double totalLength(std::vector<std::unique_ptr<Shape>>&);
	static void normalize(std::vector<std::unique_ptr<Shape>>&, double, double);
	static void normalize(std::vector<std::unique_ptr<Shape>>&);
	static double height(std::vector<std::unique_ptr<Shape>>&);
	static double width(std::vector<std::unique_ptr<Shape>>&);
	static Point maxVector(std::vector<std::unique_ptr<Shape>>&);
	static void removeOutOfBounds(std::vector<std::unique_ptr<Shape>>&);

	const double INVALID_LENGTH = -1.0;

	double len = INVALID_LENGTH;
};