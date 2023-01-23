#include "MoveTo.h"
#include "LineTo.h"

std::vector<std::unique_ptr<Shape>> MoveTo::absolute(SvgState& state, std::vector<float>& args) {
	return parseMoveTo(state, args, true);
}

std::vector<std::unique_ptr<Shape>> MoveTo::relative(SvgState& state, std::vector<float>& args) {
	return parseMoveTo(state, args, false);
}

std::vector<std::unique_ptr<Shape>> MoveTo::parseMoveTo(SvgState& state, std::vector<float>& args, bool isAbsolute) {
    if (args.size() % 2 != 0 || args.size() < 2) {
        return {};
    }

    Vector2 vec(args[0], args[1]);

    if (isAbsolute) {
        state.currPoint = vec;
        state.initialPoint = state.currPoint;
        if (args.size() > 2) {
			std::vector<float> newArgs = std::vector<float>(args.begin() + 2, args.end());
            return LineTo::absolute(state, newArgs);
        }
    } else {
		state.currPoint.translate(vec.x, vec.y);
        state.initialPoint = state.currPoint;
        if (args.size() > 2) {
            std::vector<float> newArgs = std::vector<float>(args.begin() + 2, args.end());
            return LineTo::relative(state, newArgs);
        }
    }

    return {};
}
