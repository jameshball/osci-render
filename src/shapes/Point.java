package shapes;

import java.util.Objects;

public class Point {
  private final double x;
  private final double y;

  private static final double EPSILON = 0.001;

  public Point(double x, double y) {
    this.x = x;
    this.y = y;
  }

  public double getX() {
    return x;
  }

  public double getY() {
    return y;
  }

  public Point setX(double x) {
    return new Point(x, this.y);
  }

  public Point setY(double y) {
    return new Point(this.x, y);
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Point) {
      Point point = (Point) obj;

      return approxEquals(point.getX(), getX()) && approxEquals(point.getY(), getY());
    } else {
      return false;
    }
  }

  @Override
  public int hashCode() {
    return Objects.hash(round(getX()), round(getY()));
  }

  private double round(double d) {
    return Math.round(d * 1000) / 1000d;
  }

  public Point copy() {
    return new Point(x, y);
  }

  public Point rotate(double theta) {
    Point point = setX(getX() * Math.cos(theta) - getY() * Math.sin(theta));
    point = point.setY(getX() * Math.sin(theta) + getY() * Math.cos(theta));

    return point;
  }

  private boolean approxEquals(double m, double n) {
    return Math.abs(m - n) < EPSILON;
  }
}
