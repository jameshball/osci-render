#include "Camera.h"
#include "../shape/Line.h"
#include <numbers>

Camera::Camera(double focalLength, double x, double y, double z) : focalLength(focalLength), x(x), y(y), z(z) {}

std::vector<std::unique_ptr<Shape>> Camera::draw(WorldObject& object) {
	std::vector<std::unique_ptr<Shape>> shapes;
	for (auto& edge : object.edges) {
        Vector2 start = project(object.rotateX, object.rotateY, object.rotateZ, edge.x1, edge.y1, edge.z1);
        Vector2 end = project(object.rotateX, object.rotateY, object.rotateZ, edge.x2, edge.y2, edge.z2);

		shapes.push_back(std::make_unique<Line>(start.x, start.y, end.x, end.y));
	}
	return shapes;
}

void Camera::findZPos(WorldObject& object) {
    x = 0.0;
    y = 0.0;
    z = 0.0;

    std::vector<Vector2> vertices;

    int stepsMade = 0;
    while (maxVertexValue(vertices) > VERTEX_VALUE_THRESHOLD && stepsMade < MAX_NUM_STEPS) {
        z += CAMERA_MOVE_INCREMENT;
        vertices = sampleVerticesInRender(object);
        stepsMade++;
    }
}

std::vector<Vector2> Camera::sampleVerticesInRender(WorldObject& object) {
    double rotation = 2.0 * std::numbers::pi / SAMPLE_RENDER_SAMPLES;

    std::vector<Vector2> vertices;
    
    double oldRotateX = object.rotateX;
    double oldRotateY = object.rotateY;
    double oldRotateZ = object.rotateZ;

    for (int i = 0; i < SAMPLE_RENDER_SAMPLES - 1; i++) {
        for (size_t j = 0; j < std::min(VERTEX_SAMPLES, object.numVertices); j++) {
            double x = object.vs[j * 3];
            double y = object.vs[j * 3 + 1];
            double z = object.vs[j * 3 + 2];
            vertices.push_back(project(object.rotateX, object.rotateY, object.rotateZ, x, y, z));
        }
        object.rotateY += rotation;
        object.rotateZ += rotation;
    }

    return vertices;
}

double Camera::maxVertexValue(std::vector<Vector2>& vertices) {
    if (vertices.empty()) {
        return std::numeric_limits<double>::infinity();
    }
    double max = 0.0;
    for (auto& vertex : vertices) {
        max = std::max(max, std::max(std::abs(vertex.x), std::abs(vertex.y)));
    }
    return max;
}

Vector2 Camera::project(double objRotateX, double objRotateY, double objRotateZ, double x, double y, double z) {
    // rotate around x-axis
    double cosValue = std::cos(objRotateX);
    double sinValue = std::sin(objRotateX);
    double y2 = cosValue * y - sinValue * z;
    double z2 = sinValue * y + cosValue * z;

    // rotate around y-axis
    cosValue = std::cos(objRotateY);
    sinValue = std::sin(objRotateY);
    double x2 = cosValue * x + sinValue * z2;
    double z3 = -sinValue * x + cosValue * z2;

    // rotate around z-axis
    cosValue = cos(objRotateZ);
    sinValue = sin(objRotateZ);
    double x3 = cosValue * x2 - sinValue * y2;
    double y3 = sinValue * x2 + cosValue * y2;

    double start = x3 * focalLength / (z3 - this->z) + this->x;
    double end = y3 * focalLength / (z3 - this->z) + this->y;

    return Vector2(start, end);
}
