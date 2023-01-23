#include "EllipticalArcTo.h"

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::absolute(SvgState& state, std::vector<float>& args) {
	return std::vector<std::unique_ptr<Shape>>();
}

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::relative(SvgState& state, std::vector<float>& args) {
	return std::vector<std::unique_ptr<Shape>>();
}

std::vector<std::unique_ptr<Shape>> EllipticalArcTo::parseEllipticalArc(SvgState& state, std::vector<float>& args, bool isAbsolute) {
	return std::vector<std::unique_ptr<Shape>>();
}
