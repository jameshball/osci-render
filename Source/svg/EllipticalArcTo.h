#pragma once
#include <vector>
#include "../shape/Shape.h"
#include "SvgState.h"

class EllipticalArcTo {
public:
	static std::vector<std::unique_ptr<Shape>> absolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> relative(SvgState& state, std::vector<float>& args);
private:
	static std::vector<std::unique_ptr<Shape>> parseEllipticalArc(SvgState& state, std::vector<float>& args, bool isAbsolute);
	static void createArc(std::vector<std::unique_ptr<Shape>>& shapes, Vector2 start, float rx, float ry, float theta, bool largeArcFlag, bool sweepFlag, Vector2 end);
};