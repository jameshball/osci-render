package shapes;

import java.util.Objects;

public final class Vector2 extends Shape {

  private final double x;
  private final double y;

  public Vector2(double x, double y, double weight) {
    this.x = x;
    this.y = y;
    this.weight = weight;
  }

  public Vector2(double x, double y) {
    this(x, y, Shape.DEFAULT_WEIGHT);
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

  public Vector2 copy() {
    return new Vector2(x, y);
  }

  public Vector2 add(Vector2 vector) {
    return translate(vector);
  }

  public Vector2 sub(Vector2 vector) {
    return new Vector2(getX() - vector.getX(), getY() - vector.getY());
  }

  public Vector2 reflectRelativeToVector(Vector2 vector) {
    return translate(vector.sub(this).scale(2));
  }

  @Override
  public float nextX(double drawingProgress) {
    return (float) getX();
  }

  @Override
  public float nextY(double drawingProgress) {
    return (float) getY();
  }

  @Override
  public Vector2 rotate(double theta) {
    Vector2 vector = setX(getX() * Math.cos(theta) - getY() * Math.sin(theta));
    vector = vector.setY(getX() * Math.sin(theta) + getY() * Math.cos(theta));

    return vector;
  }

  @Override
  public Vector2 scale(double factor) {
    return new Vector2(getX() * factor, getY() * factor);
  }

  @Override
  public Vector2 scale(Vector2 vector) {
    return new Vector2(getX() * vector.getX(), getY() * vector.getY());
  }

  @Override
  public Vector2 translate(Vector2 vector) {
    return new Vector2(getX() + vector.getX(), getY() + vector.getY());
  }

  @Override
  public Vector2 setWeight(double weight) {
    return new Vector2(x, y, weight);
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (obj == null || getClass() != obj.getClass()) {
      return false;
    }
    Vector2 point = (Vector2) obj;
    return round(x, 2) == round(point.x, 2)
        && round(y, 2) == round(point.y, 2);
  }

  @Override
  public int hashCode() {
    return Objects.hash(round(x, 2), round(y, 2));
  }

  private static double round(double value, int places) {
    if (places < 0) {
      throw new IllegalArgumentException();
    }

    long factor = (long) Math.pow(10, places);
    value *= factor;

    return (double) Math.round(value) / factor;
  }

  @Override
  public String toString() {
    return "Vector2{" +
        "x=" + x +
        ", y=" + y +
        '}';
  }
}
