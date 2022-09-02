package sh.ball.shapes;

import java.util.ArrayList;
import java.util.List;

public final class Line extends Shape {

  private final Vector2 a;
  private final Vector2 b;

  private double length = INVALID_LENGTH;

  public Line(Vector2 a, Vector2 b) {
    this.a = a;
    this.b = b;
  }

  public Line(double x1, double y1, double x2, double y2) {
    this(new Vector2(x1, y1), new Vector2(x2, y2));
  }

  @Override
  public Line rotate(double theta) {
    return new Line(a.rotate(theta), b.rotate(theta));
  }

  @Override
  public Line translate(Vector2 vector) {
    return new Line(a.translate(vector), b.translate(vector));
  }

  @Override
  public double getLength() {
    if (length == INVALID_LENGTH) {
      // Euclidean distance approximation based on octagonal boundary
      double dx = Math.abs(b.x - a.x);
      double dy = Math.abs(b.y - a.y);

      length = 0.41 * Math.min(dx, dy) + 0.941246 * Math.max(dx, dy);
    }
    return length;
  }

  @Override
  public Line scale(double factor) {
    return new Line(a.scale(factor), b.scale(factor));
  }

  @Override
  public Line scale(Vector2 vector) {
    return new Line(a.scale(vector), b.scale(vector));
  }

  public Line copy() {
    return new Line(a.copy(), b.copy());
  }

  @Override
  public Vector2 nextVector(double drawingProgress) {
    return new Vector2(
      a.x + drawingProgress * (b.x - a.x),
      a.y + drawingProgress * (b.y - a.y)
    );
  }

  public Vector2 getA() {
    return a;
  }

  public Vector2 getB() {
    return b;
  }

  public double getX1() {
    return a.getX();
  }

  public double getY1() {
    return a.getY();
  }

  public double getX2() {
    return b.getX();
  }

  public double getY2() {
    return b.getY();
  }

  public Line setX1(double x1) {
    return new Line(x1, getY1(), getX2(), getY2());
  }

  public Line setY1(double y1) {
    return new Line(getX1(), y1, getX2(), getY2());
  }

  public Line setX2(double x2) {
    return new Line(getX1(), getY1(), x2, getY2());
  }

  public Line setY2(double y2) {
    return new Line(getX1(), getY1(), getX2(), y2);
  }

  public static List<Line> pathToLines(double... path) {
    List<Line> lines = new ArrayList<>();

    Vector2 prev = new Vector2(path[0], path[1]);

    for (int i = 2; i < path.length; i += 2) {
      Vector2 dest = new Vector2(path[i], path[i + 1]);
      lines.add(new Line(prev, dest));
      prev = dest;
    }

    return lines;
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Line) {
      Line line = (Line) obj;

      return line.a.equals(a) && line.b.equals(b);
    } else {
      return false;
    }
  }

  @Override
  public String toString() {
    return "Line{" +
      "a=" + a +
      ", b=" + b +
      '}';
  }
}
