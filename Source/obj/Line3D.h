#pragma once

#include <JuceHeader.h>

class Line3D {
public:
	Line3D(double, double, double, double, double, double);

    void rotate(double, double, double);
	
	double x1, y1, z1, x2, y2, z2;
};

class Vector3D {
public:
    Vector3D(double, double, double);

    void rotate(double, double, double);
    
    double x, y, z;
};