#pragma once
#include <vector>
#include "../shape/Shape.h"
#include "SvgState.h"

class CurveTo {
public:
	static std::vector<std::unique_ptr<Shape>> absolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> relative(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> smoothAbsolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> smoothRelative(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> quarticAbsolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> quarticRelative(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> quarticSmoothAbsolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> quarticSmoothRelative(SvgState& state, std::vector<float>& args);
private:
	static std::vector<std::unique_ptr<Shape>> parseCurveTo(SvgState& state, std::vector<float>& args, bool isAbsolute, bool isCubic, bool isSmooth);
};