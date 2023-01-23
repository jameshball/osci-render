#include "CurveTo.h"
#include "../shape/CubicBezierCurve.h"
#include "../shape/QuadraticBezierCurve.h"

std::vector<std::unique_ptr<Shape>> CurveTo::absolute(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, true, true, false);
}

std::vector<std::unique_ptr<Shape>> CurveTo::relative(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, false, true, false);
}

std::vector<std::unique_ptr<Shape>> CurveTo::smoothAbsolute(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, true, true, true);
}

std::vector<std::unique_ptr<Shape>> CurveTo::smoothRelative(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, false, true, true);
}

std::vector<std::unique_ptr<Shape>> CurveTo::quarticAbsolute(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, true, false, false);
}

std::vector<std::unique_ptr<Shape>> CurveTo::quarticRelative(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, false, false, false);
}

std::vector<std::unique_ptr<Shape>> CurveTo::quarticSmoothAbsolute(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, true, false, true);
}

std::vector<std::unique_ptr<Shape>> CurveTo::quarticSmoothRelative(SvgState& state, std::vector<float>& args) {
    return parseCurveTo(state, args, false, false, true);
}


// Parses curveto commands (C, c, S, s, Q, q, T, and t commands)
// isCubic should be true for parsing C, c, S, and s commands
// isCubic should be false for parsing Q, q, T, and t commands
// isSmooth should be true for parsing S, s, T, and t commands
// isSmooth should be false for parsing C, c, Q, and q commands
std::vector<std::unique_ptr<Shape>> CurveTo::parseCurveTo(SvgState& state, std::vector<float>& args, bool isAbsolute, bool isCubic, bool isSmooth) {
    int expectedArgs = isCubic ? 4 : 2;
    if (!isSmooth) {
        expectedArgs += 2;
    }

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
        return {};
    }

    auto curves = std::vector<std::unique_ptr<Shape>>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
        Vector2 controlPoint1;
        Vector2 controlPoint2;

        if (isSmooth) {
            if (isCubic) {
                if (!state.prevCubicControlPoint.has_value()) {
                    controlPoint1 = state.currPoint;
                } else {
                    controlPoint1 = state.prevCubicControlPoint.value();
                    controlPoint1.reflectRelativeToVector(state.currPoint.x, state.currPoint.y);
                }
            } else {
                if (!state.prevQuadraticControlPoint.has_value()) {
                    controlPoint1 = state.currPoint;
                } else {
                    controlPoint1 = state.prevQuadraticControlPoint.value();
                    controlPoint1.reflectRelativeToVector(state.currPoint.x, state.currPoint.y);
                }
            }
        } else {
            controlPoint1 = Vector2(args[i], args[i + 1]);
        }

        if (isCubic) {
            controlPoint2 = Vector2(args[i + 2], args[i + 3]);
        }

        Vector2 newPoint(args[i + expectedArgs - 2], args[i + expectedArgs - 1]);

        if (!isAbsolute) {
			controlPoint1.translate(state.currPoint.x, state.currPoint.y);
            controlPoint2.translate(state.currPoint.x, state.currPoint.y);
            newPoint.translate(state.currPoint.x, state.currPoint.y);
        }

        if (isCubic) {
			curves.push_back(std::make_unique<CubicBezierCurve>(state.currPoint.x, state.currPoint.y, controlPoint1.x, controlPoint1.y, controlPoint2.x, controlPoint2.y, newPoint.x, newPoint.y));
            state.currPoint = newPoint;
            state.prevCubicControlPoint = controlPoint2;
        } else {
            curves.push_back(std::make_unique<QuadraticBezierCurve>(state.currPoint.x, state.currPoint.y, controlPoint1.x, controlPoint1.y, newPoint.x, newPoint.y));
            state.currPoint = newPoint;
            state.prevQuadraticControlPoint = controlPoint1;
        }
    }

    return curves;
}