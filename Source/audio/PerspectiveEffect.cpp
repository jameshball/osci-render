#include "PerspectiveEffect.h"
#include <numbers>
#include "../MathUtil.h"

PerspectiveEffect::PerspectiveEffect(int versionHint) {
    fixedRotateX = new BooleanParameter("Perspective Fixed Rotate X", "perspectiveFixedRotateX", versionHint, false);
    fixedRotateY = new BooleanParameter("Perspective Fixed Rotate Y", "perspectiveFixedRotateY", versionHint, false);
    fixedRotateZ = new BooleanParameter("Perspective Fixed Rotate Z", "perspectiveFixedRotateZ", versionHint, false);
}

PerspectiveEffect::~PerspectiveEffect() {}

Point PerspectiveEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	auto effectScale = values[0];
	auto focalLength = values[1];
	auto depth = 1.0 + (values[2] - 0.1) * 3;
	auto rotateSpeed = linearSpeedToActualSpeed(values[3]);
	double baseRotateX, baseRotateY, baseRotateZ;
	if (fixedRotateX->getBoolValue()) {
		baseRotateX = 0;
		currentRotateX = values[4] * std::numbers::pi;
	} else {
        baseRotateX = values[4] * std::numbers::pi;
    }
	if (fixedRotateY->getBoolValue()) {
		baseRotateY = 0;
        currentRotateY = values[5] * std::numbers::pi;
	} else {
        baseRotateY = values[5] * std::numbers::pi;
	}
	if (fixedRotateZ->getBoolValue()) {
        baseRotateZ = 0;
        currentRotateZ = values[6] * std::numbers::pi;
	} else {
        baseRotateZ = values[6] * std::numbers::pi;
    }

	currentRotateX = MathUtil::wrapAngle(currentRotateX + baseRotateX * rotateSpeed);
	currentRotateY = MathUtil::wrapAngle(currentRotateY + baseRotateY * rotateSpeed);
	currentRotateZ = MathUtil::wrapAngle(currentRotateZ + baseRotateZ * rotateSpeed);

	auto x = input.x;
	auto y = input.y;
	auto z = input.z;

	auto rotateX = baseRotateX + currentRotateX;
	auto rotateY = baseRotateY + currentRotateY;
	auto rotateZ = baseRotateZ + currentRotateZ;

	// rotate around x-axis
	double cosValue = std::cos(rotateX);
	double sinValue = std::sin(rotateX);
	double y2 = cosValue * y - sinValue * z;
	double z2 = sinValue * y + cosValue * z;

	// rotate around y-axis
	cosValue = std::cos(rotateY);
	sinValue = std::sin(rotateY);
	double x2 = cosValue * x + sinValue * z2;
	double z3 = -sinValue * x + cosValue * z2;

	// rotate around z-axis
	cosValue = cos(rotateZ);
	sinValue = sin(rotateZ);
	double x3 = cosValue * x2 - sinValue * y2;
	double y3 = sinValue * x2 + cosValue * y2;

	return Point(
		(1 - effectScale) * input.x + effectScale * (x3 * focalLength / (z3 - depth)),
		(1 - effectScale) * input.y + effectScale * (y3 * focalLength / (z3 - depth)),
		0
	);
}

void PerspectiveEffect::resetRotation() {
	currentRotateX = 0;
    currentRotateY = 0;
    currentRotateZ = 0;
}
