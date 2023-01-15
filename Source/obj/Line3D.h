#pragma once

#include <JuceHeader.h>

class Line3D {
public:
	Line3D(double, double, double, double, double, double);
	
	double x1, y1, z1, x2, y2, z2;
};