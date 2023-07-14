#pragma once

#include "Line3D.h"

class WorldObject {
public:
	WorldObject(std::string);
    
    void setBaseRotationX(double x);
    void setBaseRotationY(double y);
    void setBaseRotationZ(double z);
    void setCurrentRotationX(double x);
    void setCurrentRotationY(double y);
    void setCurrentRotationZ(double z);
    void setRotationSpeed(double rotateSpeed);
    void nextFrame();

    std::atomic<double> rotateX = 0.0, rotateY = 0.0, rotateZ = 0.0;
    
    std::vector<Line3D> edges;
    std::vector<float> vs;
    int numVertices;
private:
    double linearSpeedToActualSpeed(double rotateSpeed);

    std::atomic<double> baseRotateX = 0.0, baseRotateY = 0.0, baseRotateZ = 0.0;
    std::atomic<double> currentRotateX = 0.0, currentRotateY = 0.0, currentRotateZ = 0.0;
    std::atomic<double> rotateSpeed;
};