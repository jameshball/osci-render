package sh.ball.parser.svg;

import java.util.ArrayList;
import java.util.List;

import sh.ball.shapes.Shape;

class EllipticalArcTo {

  private static List<Shape> parseEllipticalArc(SvgState state, List<Float> args, boolean isAbsolute) {
    // TODO: Properly implement

    if (args.size() % 7 != 0 || args.size() < 7) {
      throw new IllegalArgumentException(
        "SVG elliptical arc command has incorrect number of arguments.");
    }

    List<Float> lineToArgs = new ArrayList<>();

    for (int i = 0; i < args.size(); i += 7) {
      lineToArgs.add(args.get(i + 5));
      lineToArgs.add(args.get(i + 6));
    }

    return isAbsolute ? LineTo.absolute(state, lineToArgs) : LineTo.relative(state, lineToArgs);
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseEllipticalArc(state, args, true);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseEllipticalArc(state, args, false);
  }
}
