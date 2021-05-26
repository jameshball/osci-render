package sh.ball.parser.svg;

import sh.ball.shapes.Vector2;

/* Intentionally package-private class for carrying around the current state of SVG parsing. */
class SvgState {

  Vector2 currPoint;
  Vector2 initialPoint;
  Vector2 prevCubicControlPoint;
  Vector2 prevQuadraticControlPoint;
}
