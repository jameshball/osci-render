package sh.ball.parser.svg;

import java.awt.geom.AffineTransform;
import java.awt.geom.Arc2D;
import java.util.ArrayList;
import java.util.List;

import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

class EllipticalArcTo {

  private static final int EXPECTED_ARGS = 7;

  private static List<Shape> parseEllipticalArc(SvgState state, List<Float> args, boolean isAbsolute) {
    if (args.size() % EXPECTED_ARGS != 0 || args.size() < EXPECTED_ARGS) {
      throw new IllegalArgumentException(
        "SVG elliptical arc command has incorrect number of arguments.");
    }

    List<Shape> arcs = new ArrayList<>();

    for (int i = 0; i < args.size(); i += EXPECTED_ARGS) {
      Vector2 newPoint = new Vector2(args.get(i + 5), args.get(i + 6));

      newPoint = isAbsolute ? newPoint : state.currPoint.translate(newPoint);

      arcs.addAll(createArc(
        state.currPoint,
        args.get(i),
        args.get(i + 1),
        args.get(i + 2),
        args.get(i + 3) == 1,
        args.get(i + 4) == 1,
        newPoint
      ));

      state.currPoint = newPoint;
    }

    return arcs;
  }

  // The following algorithm is completely based on https://www.w3.org/TR/SVG11/implnote.html#ArcImplementationNotes
  private static List<Shape> createArc(Vector2 start, double rx, double ry, float theta, boolean largeArcFlag, boolean sweepFlag, Vector2 end) {
    double x2 = end.getX();
    double y2 = end.getY();
    // Ensure radii are valid
    if (rx == 0 || ry == 0) {
      return List.of(new Line(start, end));
    }
    double x1 = start.getX();
    double y1 = start.getY();
    // Compute the half distance between the current and the final point
    double dx2 = (x1 - x2) / 2.0;
    double dy2 = (y1 - y2) / 2.0;
    // Convert theta from degrees to radians
    theta = (float) Math.toRadians(theta % 360);

    //
    // Step 1 : Compute (x1', y1')
    //
    double x1prime = Math.cos(theta) * dx2 + Math.sin(theta) * dy2;
    double y1prime = -Math.sin(theta) * dx2 + Math.cos(theta) * dy2;
    // Ensure radii are large enough
    rx = Math.abs(rx);
    ry = Math.abs(ry);
    double Prx = rx * rx;
    double Pry = ry * ry;
    double Px1prime = x1prime * x1prime;
    double Py1prime = y1prime * y1prime;
    double d = Px1prime / Prx + Py1prime / Pry;
    if (d > 1) {
      rx = Math.abs(Math.sqrt(d) * rx);
      ry = Math.abs(Math.sqrt(d) * ry);
      Prx = rx * rx;
      Pry = ry * ry;
    }

    //
    // Step 2 : Compute (cx', cy')
    //
    double sign = (largeArcFlag == sweepFlag) ? -1.0 : 1.0;
    // Forcing the inner term to be positive. It should be >= 0 but can sometimes be negative due
    // to double precision.
    double coef = sign *
      Math.sqrt(Math.abs((Prx * Pry) - (Prx * Py1prime) - (Pry * Px1prime)) / ((Prx * Py1prime) + (Pry * Px1prime)));
    double cxprime = coef * ((rx * y1prime) / ry);
    double cyprime = coef * -((ry * x1prime) / rx);

    //
    // Step 3 : Compute (cx, cy) from (cx', cy')
    //
    double sx2 = (x1 + x2) / 2.0;
    double sy2 = (y1 + y2) / 2.0;
    double cx = sx2 + Math.cos(theta) * cxprime - Math.sin(theta) * cyprime;
    double cy = sy2 + Math.sin(theta) * cxprime + Math.cos(theta) * cyprime;

    //
    // Step 4 : Compute the angleStart (theta1) and the angleExtent (dtheta)
    //
    double ux = (x1prime - cxprime) / rx;
    double uy = (y1prime - cyprime) / ry;
    double vx = (-x1prime - cxprime) / rx;
    double vy = (-y1prime - cyprime) / ry;
    double p, n;
    // Compute the angle start
    n = Math.sqrt((ux * ux) + (uy * uy));
    p = ux; // (1 * ux) + (0 * uy)
    sign = (uy < 0) ? -1.0 : 1.0;
    double angleStart = Math.toDegrees(sign * Math.acos(p / n));
    // Compute the angle extent
    n = Math.sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    p = ux * vx + uy * vy;
    sign = (ux * vy - uy * vx < 0) ? -1.0 : 1.0;
    double angleExtent = Math.toDegrees(sign * Math.acos(p / n));
    if (!sweepFlag && angleExtent > 0) {
      angleExtent -= 360;
    } else if (sweepFlag && angleExtent < 0) {
      angleExtent += 360;
    }
    angleExtent %= 360;
    angleStart %= 360;

    Arc2D.Float arc = new Arc2D.Float();
    arc.x = (float) (cx - rx);
    arc.y = (float) (cy - ry);
    arc.width = (float) (rx * 2.0);
    arc.height = (float) (ry * 2.0);
    arc.start = (float) -angleStart;
    arc.extent = (float) -angleExtent;

    AffineTransform transform = AffineTransform.getRotateInstance(theta, arc.getX() + arc.getWidth()/2, arc.getY() + arc.getHeight()/2);

    return Shape.convert(transform.createTransformedShape(arc));
  }

  static List<Shape> absolute(SvgState state, List<Float> args) {
    return parseEllipticalArc(state, args, true);
  }

  static List<Shape> relative(SvgState state, List<Float> args) {
    return parseEllipticalArc(state, args, false);
  }
}
