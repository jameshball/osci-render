#include "RotateEffect.h"
#include <numbers>
#include "../MathUtil.h"

RotateEffect::RotateEffect(int versionHint) {
    fixedRotateX = new BooleanParameter("Fixed Rotate X", "fixedRotateX", versionHint, false);
    fixedRotateY = new BooleanParameter("Fixed Rotate Y", "fixedRotateY", versionHint, false);
    fixedRotateZ = new BooleanParameter("Fixed Rotate Z", "fixedRotateZ", versionHint, false);
}

RotateEffect::~RotateEffect() {}

Point RotateEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	auto rotateSpeed = linearSpeedToActualSpeed(values[0]);
	double baseRotateX, baseRotateY, baseRotateZ;
	if (fixedRotateX->getBoolValue()) {
		baseRotateX = 0;
		currentRotateX = values[1] * std::numbers::pi;
	} else {
        baseRotateX = values[1] * std::numbers::pi;
    }
	if (fixedRotateY->getBoolValue()) {
		baseRotateY = 0;
        currentRotateY = values[2] * std::numbers::pi;
	} else {
        baseRotateY = values[2] * std::numbers::pi;
	}
	if (fixedRotateZ->getBoolValue()) {
        baseRotateZ = 0;
        currentRotateZ = values[3] * std::numbers::pi;
	} else {
        baseRotateZ = values[3] * std::numbers::pi;
    }

	currentRotateX = MathUtil::wrapAngle(currentRotateX + baseRotateX * rotateSpeed);
	currentRotateY = MathUtil::wrapAngle(currentRotateY + baseRotateY * rotateSpeed);
	currentRotateZ = MathUtil::wrapAngle(currentRotateZ + baseRotateZ * rotateSpeed);

	auto rotateX = baseRotateX + currentRotateX;
	auto rotateY = baseRotateY + currentRotateY;
	auto rotateZ = baseRotateZ + currentRotateZ;

	input.rotate(rotateX, rotateY, rotateZ);
	return input;
}

void RotateEffect::resetRotation() {
	currentRotateX = 0;
    currentRotateY = 0;
    currentRotateZ = 0;
}
