#include "Line3D.h"
#include "../shape/Point.h"

Line3D::Line3D(double x1, double y1, double z1, double x2, double y2, double z2) : x1(x1), y1(y1), z1(z1), x2(x2), y2(y2), z2(z2) {}

void Line3D::rotate(double rotateX, double rotateY, double rotateZ) {
    Point vector1(x1, y1, z1);
    Point vector2(x2, y2, z2);
    vector1.rotate(rotateX, rotateY, rotateZ);
    vector2.rotate(rotateX, rotateY, rotateZ);
    x1 = vector1.x;
    y1 = vector1.y;
    z1 = vector1.z;
    x2 = vector2.x;
    y2 = vector2.y;
    z2 = vector2.z;
}
