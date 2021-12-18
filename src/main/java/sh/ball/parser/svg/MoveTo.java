package sh.ball.parser.svg;

import java.util.ArrayList;
import java.util.List;

import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

class MoveTo {

  // Parses moveto commands (M and m commands)
  private static List<Shape> parseMoveTo(SvgState state, List<Float> args, boolean isAbsolute) {
    if (args.size() % 2 != 0 || args.size() < 2) {
      for (Float arg : args) {
        System.out.println(arg);
      }
      throw new IllegalArgumentException("SVG moveto command has incorrect number of arguments.\n");
    }

    Vector2 vec = new Vector2(args.get(0), args.get(1));

    if (isAbsolute) {
      state.currPoint = vec;
      state.initialPoint = state.currPoint;
      if (args.size() > 2) {
        return LineTo.absolute(state, args.subList(2, args.size()));
      }
    } else {
      state.currPoint = state.currPoint.translate(vec);
      state.initialPoint = state.currPoint;
      if (args.size() > 2) {
        return LineTo.relative(state, args.subList(2, args.size()));
      }
    }

    return new ArrayList<>();
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseMoveTo(state, args, true);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseMoveTo(state, args, false);
  }
}
