#include "Camera.h"
#include "../shape/Line.h"
#include <numbers>

Camera::Camera(double focalLength, double x, double y, double z) : focalLength(focalLength), x(x), y(y), z(z) {}

std::vector<std::unique_ptr<Shape>> Camera::draw(WorldObject& object) {
	std::vector<std::unique_ptr<Shape>> shapes;
    object.nextFrame();
	for (auto edge : object.edges) {
        edge.rotate(object.rotateX, object.rotateY, object.rotateZ);
        // very crude frustum culling
        double minZ = z + 0.1;
        if (edge.z1 < minZ && edge.z2 < minZ) {
            continue;
        }
        if (edge.z1 < minZ) {
            double ratio = (minZ - edge.z1) / (edge.z2 - edge.z1);
            edge.x1 = edge.x1 + (edge.x2 - edge.x1) * ratio;
            edge.y1 = edge.y1 + (edge.y2 - edge.y1) * ratio;
            edge.z1 = minZ;
        }
        if (edge.z2 < minZ) {
            double ratio = (minZ - edge.z2) / (edge.z1 - edge.z2);
            edge.x2 = edge.x2 + (edge.x1 - edge.x2) * ratio;
            edge.y2 = edge.y2 + (edge.y1 - edge.y2) * ratio;
            edge.z2 = minZ;
        }

        Vector2 start = project(edge.x1, edge.y1, edge.z1);
        Vector2 end = project(edge.x2, edge.y2, edge.z2);

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

void Camera::setFocalLength(double focalLength) {
    this->focalLength = focalLength;
}

std::vector<Vector2> Camera::sampleVerticesInRender(WorldObject& object) {
    double rotation = 2.0 * std::numbers::pi / SAMPLE_RENDER_SAMPLES;

    std::vector<Vector2> vertices;

    for (int i = 0; i < SAMPLE_RENDER_SAMPLES - 1; i++) {
        for (size_t j = 0; j < std::min(VERTEX_SAMPLES, object.numVertices); j++) {
            Vector3D vertex{object.vs[j * 3], object.vs[j * 3 + 1], object.vs[j * 3 + 2]};
            vertex.rotate(object.rotateX, object.rotateY, object.rotateZ);
            vertices.push_back(project(vertex.x, vertex.y, vertex.z));
        }
        object.rotateY = object.rotateY + rotation;
        object.rotateZ = object.rotateY + rotation;
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

Vector2 Camera::project(double x, double y, double z) {
    double start = x * focalLength / (z - this->z) + this->x;
    double end = y * focalLength / (z - this->z) + this->y;

    return Vector2(start, end);
}
