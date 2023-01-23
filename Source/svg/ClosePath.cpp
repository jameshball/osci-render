#include "ClosePath.h"
#include "../shape/Line.h"

std::vector<std::unique_ptr<Shape>> ClosePath::absolute(SvgState& state, std::vector<float>& args) {
	return parseClosePath(state, args);
}

std::vector<std::unique_ptr<Shape>> ClosePath::relative(SvgState& state, std::vector<float>& args) {
	return parseClosePath(state, args);
}

std::vector<std::unique_ptr<Shape>> ClosePath::parseClosePath(SvgState& state, std::vector<float>& args) {
	auto shapes = std::vector<std::unique_ptr<Shape>>();
	if (state.currPoint.x != state.initialPoint.x || state.currPoint.y != state.initialPoint.y) {
        state.currPoint = state.initialPoint;
		shapes.push_back(std::make_unique<Line>(state.currPoint.x, state.currPoint.y, state.initialPoint.x, state.initialPoint.y));
    }
	return shapes;
}