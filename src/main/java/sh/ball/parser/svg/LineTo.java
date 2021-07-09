package sh.ball.parser.svg;

import java.util.ArrayList;
import java.util.List;

import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

class LineTo {

  // Parses lineto commands (L, l, H, h, V, and v commands)
  // isHorizontal and isVertical should be true for parsing L and l commands
  // Only isHorizontal should be true for parsing H and h commands
  // Only isVertical should be true for parsing V and v commands
  private static List<Shape> parseLineTo(SvgState state, List<Float> args, boolean isAbsolute,
                                         boolean isHorizontal, boolean isVertical) {
    int expectedArgs = isHorizontal && isVertical ? 2 : 1;

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
      throw new IllegalArgumentException("SVG lineto command has incorrect number of arguments. Expected multiple of " + expectedArgs + ", got " + args.size());
    }

    List<Shape> lines = new ArrayList<>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
      Vector2 newPoint;

      if (expectedArgs == 1) {
        newPoint = new Vector2(args.get(i), args.get(i));
      } else {
        newPoint = new Vector2(args.get(i), args.get(i + 1));
      }

      if (isHorizontal && !isVertical) {
        newPoint = isAbsolute ? newPoint.setY(state.currPoint.getY()) : newPoint.setY(0);
      } else if (isVertical && !isHorizontal) {
        newPoint = isAbsolute ? newPoint.setX(state.currPoint.getX()) : newPoint.setX(0);
      }

      if (!isAbsolute) {
        newPoint = state.currPoint.translate(newPoint);
      }

      lines.add(new Line(state.currPoint, newPoint));
      state.currPoint = newPoint;
    }

    return lines;
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseLineTo(state, args, true, true, true);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseLineTo(state, args, false, true, true);
  }

  static List<Shape> horizontalAbsolute(SvgState state, List<Float> args) {
    return parseLineTo(state, args, true, true, false);
  }

  static List<Shape> horizontalRelative(SvgState state, List<Float> args) {
    return parseLineTo(state, args, false, true, false);
  }

  static List<Shape> verticalAbsolute(SvgState state, List<Float> args) {
    return parseLineTo(state, args, true, false, true);
  }

  static List<Shape> verticalRelative(SvgState state, List<Float> args) {
    return parseLineTo(state, args, false, false, true);
  }
}
