package shapes;

import java.util.Objects;

public class Point {
  private double x;
  private double y;

  private static final double EPSILON = 0.0001;

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

  public void setX(double x) {
    this.x = x;
  }

  public void setY(double y) {
    this.y = y;
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

  public Point copy() {
    return new Point(x, y);
  }

  public Point rotate(double theta) {
    Point point = new Point(getX(), getY());

    point.setX(getX() * Math.cos(theta) - getY() * Math.sin(theta));
    point.setY(getX() * Math.sin(theta) + getY() * Math.cos(theta));

    return point;
  }

  private boolean approxEquals(double m, double n) {
    return Math.abs(m - n) < EPSILON;
  }
}
