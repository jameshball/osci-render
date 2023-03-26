#include "Vector2.h"

Vector2::Vector2() : x(0), y(0) {}

Vector2::Vector2(double val) : x(val), y(val) {}

Vector2::Vector2(double x, double y) : x(x), y(y) {}

Vector2 Vector2::nextVector(double drawingProgress){
	return Vector2(x, y);
}

void Vector2::rotate(double theta) {
    double cosTheta = std::cos(theta);
    double sinTheta = std::sin(theta);
    
    double newX = x * cosTheta - y * sinTheta;
    double newY = x * sinTheta + y * cosTheta;

	x = newX;
	y = newY;
}

void Vector2::scale(double x, double y) {
	this->x *= x;
	this->y *= y;
}

void Vector2::translate(double x, double y) {
	this->x += x;
	this->y += y;
}

void Vector2::reflectRelativeToVector(double x, double y) {
	this->x += 2.0 * (x - this->x);
	this->y += 2.0 * (y - this->y);
}

double Vector2::length() {
	return 0.0;
}

double Vector2::magnitude() {
	return sqrt(x * x + y * y);
}
