#pragma once
#include <vector>
#include "../shape/Shape.h"
#include "SvgState.h"

class ClosePath {
public:
	static std::vector<std::unique_ptr<Shape>> absolute(SvgState& state, std::vector<float>& args);
	static std::vector<std::unique_ptr<Shape>> relative(SvgState& state, std::vector<float>& args);
private:
	static std::vector<std::unique_ptr<Shape>> parseClosePath(SvgState& state, std::vector<float>& args);
};