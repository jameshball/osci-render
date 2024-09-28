#include "Point.h"

double Point::EPSILON = 0.0001;

Point::Point() : x(0), y(0), z(0), v(0), w(0) {}

Point::Point(double val) : x(val), y(val), z(0), v(0), w(0) {}

Point::Point(double x, double y) : x(x), y(y), z(0), v(0), w(0) {}

Point::Point(double x, double y, double z) : x(x), y(y), z(z), v(0), w(0) {}

Point::Point(double x, double y, double z, double v, double w) : x(x), y(y), z(z), v(v), w(w) {}

Point Point::nextVector(double drawingProgress){
	return Point(x, y, z, v, w);
}

void Point::rotate(double rotateX, double rotateY, double rotateZ) {
    // rotate around x-axis
    double cosValue = std::cos(rotateX);
    double sinValue = std::sin(rotateX);
    double y2 = cosValue * y - sinValue * z;
    double z2 = sinValue * y + cosValue * z;

    // rotate around y-axis
    cosValue = std::cos(rotateY);
    sinValue = std::sin(rotateY);
    double x2 = cosValue * x + sinValue * z2;
    z = -sinValue * x + cosValue * z2;

    // rotate around z-axis
    cosValue = cos(rotateZ);
    sinValue = sin(rotateZ);
    x = cosValue * x2 - sinValue * y2;
    y = sinValue * x2 + cosValue * y2;
}

void Point::normalize() {
    double mag = magnitude();
    x /= mag;
    y /= mag;
    z /= mag;
}

double Point::innerProduct(Point& other) {
    return x * other.x + y * other.y + z * other.z;
}

void Point::scale(double x, double y, double z) {
	this->x *= x;
	this->y *= y;
    this->z *= z;
}

void Point::translate(double x, double y, double z) {
	this->x += x;
	this->y += y;
    this->z += z;
}

double Point::length() {
	return 0.0;
}

double Point::magnitude() {
	return sqrt(x * x + y * y + z * z);
}

std::unique_ptr<Shape> Point::clone() {
	return std::make_unique<Point>(x, y, z);
}

std::string Point::type() {
	return std::string();
}

Point& Point::operator=(const Point& other) {
	x = other.x;
	y = other.y;
	z = other.z;
    v = other.v;
    w = other.w;
	return *this;
}

bool Point::operator==(const Point& other) {
    return std::abs(x - other.x) < EPSILON && std::abs(y - other.y) < EPSILON && std::abs(z - other.z) < EPSILON;
}

bool Point::operator!=(const Point& other) {
    return !(*this == other);
}

Point Point::operator+(const Point& other) {
    return Point(x + other.x, y + other.y, z + other.z, v + other.v, w + other.w);
}

Point Point::operator+(double scalar) {
	return Point(x + scalar, y + scalar, z + scalar, v + scalar, w + scalar);
}



Point Point::operator-(const Point& other) {
    return Point(x - other.x, y - other.y, z - other.z, v - other.v, w - other.w);
}

Point Point::operator-() {
    return Point(-x, -y, -z, -v, -w);
}

Point Point::operator*(const Point& other) {
    return Point(x * other.x, y * other.y, z * other.z, v * other.v, w * other.w);
}

Point Point::operator*(double scalar) {
    return Point(x * scalar, y * scalar, z * scalar, v * scalar, w * scalar);
}

std::string Point::toString() {
    if (v != 0 || w != 0) {
        return std::string("(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + "), <" + std::to_string(v) + ", " + std::to_string(w) + ">");
    }
    else {
        return std::string("(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
    }
}

Point operator+(double scalar, const Point& point) {
	return Point(point.x + scalar, point.y + scalar, point.z + scalar, point.v + scalar, point.w + scalar);
}

Point operator*(double scalar, const Point& point) {
	return Point(point.x * scalar, point.y * scalar, point.z * scalar, point.v * scalar, point.w * scalar);
}
