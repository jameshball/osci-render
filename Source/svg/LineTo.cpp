#include "LineTo.h"
#include "../shape/Line.h"

std::vector<std::unique_ptr<Shape>> LineTo::absolute(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, true, true, true);
}

std::vector<std::unique_ptr<Shape>> LineTo::relative(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, false, true, true);
}

std::vector<std::unique_ptr<Shape>> LineTo::horizontalAbsolute(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, true, true, false);
}

std::vector<std::unique_ptr<Shape>> LineTo::horizontalRelative(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, false, true, false);
}

std::vector<std::unique_ptr<Shape>> LineTo::verticalAbsolute(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, true, false, true);
}

std::vector<std::unique_ptr<Shape>> LineTo::verticalRelative(SvgState& state, std::vector<float>& args) {
	return parseLineTo(state, args, false, false, true);
}


// Parses lineto commands (L, l, H, h, V, and v commands)
// isHorizontal and isVertical should be true for parsing L and l commands
// Only isHorizontal should be true for parsing H and h commands
// Only isVertical should be true for parsing V and v commands
std::vector<std::unique_ptr<Shape>> LineTo::parseLineTo(SvgState& state, std::vector<float>& args, bool isAbsolute, bool isHorizontal, bool isVertical) {
    int expectedArgs = isHorizontal && isVertical ? 2 : 1;

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
        return {};
    }

    auto lines = std::vector<std::unique_ptr<Shape>>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
        Vector2 newPoint;

        if (expectedArgs == 1) {
            newPoint = Vector2(args[i], args[i]);
        } else {
            newPoint = Vector2(args[i], args[i + 1]);
        }

        if (isHorizontal && !isVertical) {
            if (isAbsolute) {
                newPoint.y = state.currPoint.y;
            } else {
                newPoint.y = 0;
            }
        } else if (isVertical && !isHorizontal) {
            if (isAbsolute) {
                newPoint.x = state.currPoint.x;
            } else {
                newPoint.x = 0;
            }
        }

        if (!isAbsolute) {
			newPoint.translate(state.currPoint.x, state.currPoint.y);
        }

		lines.push_back(std::make_unique<Line>(state.currPoint.x, state.currPoint.y, newPoint.x, newPoint.y));
        state.currPoint = newPoint;
    }

    return lines;
}
