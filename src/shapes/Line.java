package shapes;

import java.util.ArrayList;
import java.util.List;

public final class Line extends Shape {

  private final Vector2 a;
  private final Vector2 b;

  public Line(Vector2 a, Vector2 b, double weight) {
    this.a = a;
    this.b = b;
    this.weight = weight;
    this.length = calculateLength();
  }

  public Line(Vector2 a, Vector2 b) {
    this(a, b, Shape.DEFAULT_WEIGHT);
  }

  public Line(double x1, double y1, double x2, double y2, double weight) {
    this(new Vector2(x1, y1), new Vector2(x2, y2), weight);
  }

  public Line(double x1, double y1, double x2, double y2) {
    this(new Vector2(x1, y1), new Vector2(x2, y2));
  }

  private double calculateLength() {
    return Math.sqrt(Math.pow(getX1() - getX2(), 2) + Math.pow(getY1() - getY2(), 2));
  }

  @Override
  public Line rotate(double theta) {
    return new Line(a.rotate(theta), b.rotate(theta), weight);
  }

  @Override
  public Line translate(Vector2 vector) {
    return new Line(a.translate(vector), b.translate(vector), weight);
  }

  @Override
  public Line scale(double factor) {
    return new Line(a.scale(factor), b.scale(factor), weight);
  }

  @Override
  public Line scale(Vector2 vector) {
    return new Line(a.scale(vector), b.scale(vector), weight);
  }

  public Line copy() {
    return new Line(a.copy(), b.copy(), weight);
  }

  @Override
  public float nextX(double drawingProgress) {
    return (float) (getX1() + (getX2() - getX1()) * drawingProgress);
  }

  @Override
  public float nextY(double drawingProgress) {
    return (float) (getY1() + (getY2() - getY1()) * drawingProgress);
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
  public Line setWeight(double weight) {
    return new Line(getX1(), getY1(), getX2(), getY2(), weight);
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
