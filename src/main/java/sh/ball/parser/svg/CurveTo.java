package sh.ball.parser.svg;

import java.util.ArrayList;
import java.util.List;
import sh.ball.shapes.CubicBezierCurve;
import sh.ball.shapes.QuadraticBezierCurve;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

class CurveTo {

  // Parses curveto commands (C, c, S, s, Q, q, T, and t commands)
  // isCubic should be true for parsing C, c, S, and s commands
  // isCubic should be false for parsing Q, q, T, and t commands
  // isSmooth should be true for parsing S, s, T, and t commands
  // isSmooth should be false for parsing C, c, Q, and q commands
  private static List<Shape> parseCurveTo(SvgState state, List<Float> args, boolean isAbsolute,
      boolean isCubic, boolean isSmooth) {
    int expectedArgs = isCubic ? 4 : 2;
    if (!isSmooth) {
      expectedArgs += 2;
    }

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
      throw new IllegalArgumentException("SVG curveto command has incorrect number of arguments.");
    }

    List<Shape> curves = new ArrayList<>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
      Vector2 controlPoint1;
      Vector2 controlPoint2 = new Vector2();

      if (isSmooth) {
        if (isCubic) {
          controlPoint1 = state.prevCubicControlPoint == null ? state.currPoint
              : state.prevCubicControlPoint.reflectRelativeToVector(state.currPoint);
        } else {
          controlPoint1 = state.prevQuadraticControlPoint == null ? state.currPoint
              : state.prevQuadraticControlPoint.reflectRelativeToVector(state.currPoint);
        }
      } else {
        controlPoint1 = new Vector2(args.get(i), args.get(i + 1));
      }

      if (isCubic) {
        controlPoint2 = new Vector2(args.get(i + 2), args.get(i + 3));
      }

      Vector2 newPoint = new Vector2(args.get(i + expectedArgs - 2),
          args.get(i + expectedArgs - 1));

      if (!isAbsolute) {
        controlPoint1 = state.currPoint.translate(controlPoint1);
        controlPoint2 = state.currPoint.translate(controlPoint2);
        newPoint = state.currPoint.translate(newPoint);
      }

      if (isCubic) {
        curves.add(new CubicBezierCurve(state.currPoint, controlPoint1, controlPoint2, newPoint));
        state.currPoint = newPoint;
        state.prevCubicControlPoint = controlPoint2;
      } else {
        curves.add(new QuadraticBezierCurve(state.currPoint, controlPoint1, newPoint));
        state.currPoint = newPoint;
        state.prevQuadraticControlPoint = controlPoint1;
      }
    }

    return curves;
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, true, true, false);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, false, true, false);
  }

  static List<Shape> smoothAbsolute(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, true, true, true);
  }

  static List<Shape> smoothRelative(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, false, true, true);
  }

  static List<Shape> quarticAbsolute(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, true, false, false);
  }

  static List<Shape> quarticRelative(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, false, false, false);
  }

  static List<Shape> quarticSmoothAbsolute(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, true, false, true);
  }

  static List<Shape> quarticSmoothRelative(SvgState state, List<Float> args) {
    return parseCurveTo(state, args, false, false, true);
  }
}
