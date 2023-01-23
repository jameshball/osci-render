#pragma once
#include <vector>
#include "../shape/Shape.h"
#include "SvgState.h"

class LineTo {
public:
	static std::vector<std::unique_ptr<Shape>> absolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> relative(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> horizontalAbsolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> horizontalRelative(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> verticalAbsolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> verticalRelative(SvgState& state, std::vector<float>& args);
private:
	static std::vector<std::unique_ptr<Shape>> parseLineTo(SvgState& state, std::vector<float>& args, bool isAbsolute, bool isHorizontal, bool isVertical);
};