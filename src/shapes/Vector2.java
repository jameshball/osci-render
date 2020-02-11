package shapes;

import java.util.Objects;

public class Vector2 {
  private final double x;
  private final double y;

  private static final double EPSILON = 0.001;

  public Vector2(double x, double y) {
    this.x = x;
    this.y = y;
  }

  public Vector2() {
    this(0, 0);
  }

  public double getX() {
    return x;
  }

  public double getY() {
    return y;
  }

  public Vector2 setX(double x) {
    return new Vector2(x, this.y);
  }

  public Vector2 setY(double y) {
    return new Vector2(this.x, y);
  }

  public Vector2 add(Vector2 vector) {
    return new Vector2(getX() + vector.getX(), getY() + vector.getY());
  }

  public Vector2 scale(double factor) {
    return new Vector2(getX() * factor, getY() * factor);
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Vector2) {
      Vector2 vector = (Vector2) obj;

      return approxEquals(vector.getX(), getX()) && approxEquals(vector.getY(), getY());
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

  public Vector2 copy() {
    return new Vector2(x, y);
  }

  public Vector2 rotate(double theta) {
    Vector2 vector = setX(getX() * Math.cos(theta) - getY() * Math.sin(theta));
    vector = vector.setY(getX() * Math.sin(theta) + getY() * Math.cos(theta));

    return vector;
  }

  private boolean approxEquals(double m, double n) {
    return Math.abs(m - n) < EPSILON;
  }
}
