#include "Line3D.h"

Line3D::Line3D(double x1, double y1, double z1, double x2, double y2, double z2) : x1(x1), y1(y1), z1(z1), x2(x2), y2(y2), z2(z2) {}

void Line3D::rotate(double rotateX, double rotateY, double rotateZ) {
    Vector3D vector1(x1, y1, z1);
    Vector3D vector2(x2, y2, z2);
    vector1.rotate(rotateX, rotateY, rotateZ);
    vector2.rotate(rotateX, rotateY, rotateZ);
    x1 = vector1.x;
    y1 = vector1.y;
    z1 = vector1.z;
    x2 = vector2.x;
    y2 = vector2.y;
    z2 = vector2.z;
}

Vector3D::Vector3D(double x, double y, double z) : x(x), y(y), z(z) {}

void Vector3D::rotate(double rotateX, double rotateY, double rotateZ) {
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
