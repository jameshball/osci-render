package sh.ball.shapes;

import java.util.Objects;

import static sh.ball.math.Math.round;

public final class Vector2 extends Shape {

  public final double x;
  public final double y;

  public Vector2(double x, double y) {
    this.x = x;
    this.y = y;
  }

  public Vector2(double xy) {
    this(xy, xy);
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
  public Vector2 nextVector(double drawingProgress) {
    return this;
  }

  @Override
  public Vector2 rotate(double theta) {
    double cosTheta = Math.cos(theta);
    double sinTheta = Math.sin(theta);
    return new Vector2(
      x * cosTheta - y * sinTheta,
      x * sinTheta + y * cosTheta
    );
  }

  @Override
  public Vector2 scale(double factor) {
    return new Vector2(x * factor, y * factor);
  }

  @Override
  public Vector2 scale(Vector2 vector) {
    return new Vector2(x * vector.x, y * vector.y);
  }

  @Override
  public Vector2 translate(Vector2 vector) {
    return new Vector2(x + vector.x, y + vector.y);
  }

  @Override
  public double getLength() {
    return 0;
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
    return x == point.x && y == point.y;
  }

  @Override
  public int hashCode() {
    return Objects.hash(x, y);
  }

  @Override
  public String toString() {
    return "Vector2{" +
      "x=" + x +
      ", y=" + y +
      '}';
  }

  public double magnitude() {
    return Math.sqrt(x * x + y * y);
  }
}
