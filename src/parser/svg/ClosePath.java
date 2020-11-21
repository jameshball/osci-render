package parser.svg;

import java.util.List;
import shapes.Line;
import shapes.Shape;

class ClosePath {

  // Parses close path commands (Z and z commands)
  private static List<Shape> parseClosePath(SvgState state, List<Float> args) {
    if (!state.currPoint.equals(state.initialPoint)) {
      Line line = new Line(state.currPoint, state.initialPoint);
      state.currPoint = state.initialPoint;
      return List.of(line);
    } else {
      return List.of();
    }
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseClosePath(state, args);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseClosePath(state, args);
  }
}
