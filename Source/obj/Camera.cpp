#include "Camera.h"

Camera::Camera() : frustum(1, 1, 0.1, 100) {
    viewMatrix = mathter::Identity();
}

void Camera::setPosition(Vec3& position) {
    viewMatrix(0, 3) = -position.x;
    viewMatrix(1, 3) = -position.y;
    viewMatrix(2, 3) = -position.z;
}

Vec3 Camera::toCameraSpace(Vec3& point) {
    return viewMatrix * point;
}

Vec3 Camera::toWorldSpace(Vec3& point) {
    return mathter::Inverse(viewMatrix) * point;
}

void Camera::setFocalLength(double focalLength) {
    frustum.setCameraInternals(focalLength, frustum.ratio, frustum.nearDistance, frustum.farDistance);
}

Vec3 Camera::project(Vec3& pWorld) {
    Vec3 p = viewMatrix * pWorld;

    frustum.clipToFrustum(p);

    double start = p.x * frustum.focalLength / p.z;
    double end = p.y * frustum.focalLength / p.z;

    return Vec3(start, end, 0);
}

Frustum Camera::getFrustum() {
    return frustum;
}
