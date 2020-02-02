package shapes;

import java.util.Objects;

public class Vector {
  private final double x;
  private final double y;

  private static final double EPSILON = 0.001;

  public Vector(double x, double y) {
    this.x = x;
    this.y = y;
  }

  public double getX() {
    return x;
  }

  public double getY() {
    return y;
  }

  public Vector setX(double x) {
    return new Vector(x, this.y);
  }

  public Vector setY(double y) {
    return new Vector(this.x, y);
  }

  public Vector add(Vector vector) {
    return new Vector(getX() + vector.getX(), getY() + vector.getY());
  }

  public Vector scale(double factor) {
    return new Vector(getX() * factor, getY() * factor);
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Vector) {
      Vector vector = (Vector) obj;

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

  public Vector copy() {
    return new Vector(x, y);
  }

  public Vector rotate(double theta) {
    Vector vector = setX(getX() * Math.cos(theta) - getY() * Math.sin(theta));
    vector = vector.setY(getX() * Math.sin(theta) + getY() * Math.cos(theta));

    return vector;
  }

  private boolean approxEquals(double m, double n) {
    return Math.abs(m - n) < EPSILON;
  }
}
