#include "OsciPoint.h"

double OsciPoint::EPSILON = 0.0001;

OsciPoint::OsciPoint() : x(0), y(0), z(0) {}

OsciPoint::OsciPoint(double val) : x(val), y(val), z(0) {}

OsciPoint::OsciPoint(double x, double y) : x(x), y(y), z(0) {}

OsciPoint::OsciPoint(double x, double y, double z) : x(x), y(y), z(z) {}

OsciPoint OsciPoint::nextVector(double drawingProgress){
	return OsciPoint(x, y, z);
}

void OsciPoint::rotate(double rotateX, double rotateY, double rotateZ) {
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

void OsciPoint::normalize() {
    double mag = magnitude();
    x /= mag;
    y /= mag;
    z /= mag;
}

double OsciPoint::innerProduct(OsciPoint& other) {
    return x * other.x + y * other.y + z * other.z;
}

void OsciPoint::scale(double x, double y, double z) {
	this->x *= x;
	this->y *= y;
    this->z *= z;
}

void OsciPoint::translate(double x, double y, double z) {
	this->x += x;
	this->y += y;
    this->z += z;
}

double OsciPoint::length() {
	return 0.0;
}

double OsciPoint::magnitude() {
	return sqrt(x * x + y * y + z * z);
}

std::unique_ptr<Shape> OsciPoint::clone() {
	return std::make_unique<OsciPoint>(x, y, z);
}

std::string OsciPoint::type() {
	return std::string();
}

OsciPoint& OsciPoint::operator=(const OsciPoint& other) {
	x = other.x;
	y = other.y;
	z = other.z;
	return *this;
}

bool OsciPoint::operator==(const OsciPoint& other) {
    return std::abs(x - other.x) < EPSILON && std::abs(y - other.y) < EPSILON && std::abs(z - other.z) < EPSILON;
}

bool OsciPoint::operator!=(const OsciPoint& other) {
    return !(*this == other);
}

OsciPoint OsciPoint::operator+(const OsciPoint& other) {
    return OsciPoint(x + other.x, y + other.y, z + other.z);
}

OsciPoint OsciPoint::operator+(double scalar) {
	return OsciPoint(x + scalar, y + scalar, z + scalar);
}



OsciPoint OsciPoint::operator-(const OsciPoint& other) {
    return OsciPoint(x - other.x, y - other.y, z - other.z);
}

OsciPoint OsciPoint::operator-() {
    return OsciPoint(-x, -y, -z);
}

OsciPoint OsciPoint::operator*(const OsciPoint& other) {
    return OsciPoint(x * other.x, y * other.y, z * other.z);
}

OsciPoint OsciPoint::operator*(double scalar) {
    return OsciPoint(x * scalar, y * scalar, z * scalar);
}

std::string OsciPoint::toString() {
    return std::string("(" + std::to_string(x) + ", " + std::to_string(y) + ", " + std::to_string(z) + ")");
}

OsciPoint operator+(double scalar, const OsciPoint& point) {
	return OsciPoint(point.x + scalar, point.y + scalar, point.z + scalar);
}

OsciPoint operator*(double scalar, const OsciPoint& point) {
	return OsciPoint(point.x * scalar, point.y * scalar, point.z * scalar);
}
