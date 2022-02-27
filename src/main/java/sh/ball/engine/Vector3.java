package sh.ball.engine;

import java.util.List;
import java.util.Objects;

public final class Vector3 {

  private final double x, y, z;

  public Vector3(double x, double y, double z) {
    this.x = x;
    this.y = y;
    this.z = z;
  }

  public Vector3(double xyz) {
    this(xyz, xyz, xyz);
  }

  public Vector3() {
    this(0, 0, 0);
  }

  public double getX() {
    return x;
  }

  public double getY() {
    return y;
  }

  public double getZ() {
    return z;
  }

  public Vector3 add(Vector3 other) {
    return new Vector3(
      x + other.x,
      y + other.y,
      z + other.z
    );
  }

  public Vector3 scale(double factor) {
    return new Vector3(
      x * factor,
      y * factor,
      z * factor
    );
  }

  public Vector3 rotate(Vector3 rotation) {
    return rotateX(rotation.x)
      .rotateY(rotation.y)
      .rotateZ(rotation.z);
  }

  public Vector3 rotateX(double theta) {
    double cos = Math.cos(theta);
    double sin = Math.sin(theta);
    return new Vector3(
      x,
      cos * y - sin * z,
      sin * y + cos * z
    );
  }

  public Vector3 rotateY(double theta) {
    double cos = Math.cos(theta);
    double sin = Math.sin(theta);
    return new Vector3(
      cos * x + sin * z,
      y,
      -sin * x + cos * z
    );
  }

  public Vector3 rotateZ(double theta) {
    double cos = Math.cos(theta);
    double sin = Math.sin(theta);
    return new Vector3(
      cos * x - sin * y,
      sin * x + cos * y,
      z
    );
  }

  public double distance(Vector3 vector) {
    double xDiff = x - vector.x;
    double yDiff = y - vector.y;
    double zDiff = z - vector.z;
    return Math.sqrt(xDiff * xDiff + yDiff * yDiff + zDiff * zDiff);
  }

  public static Vector3 meanPoint(List<Vector3> points) {
    Vector3 mean = new Vector3();

    for (Vector3 point : points) {
      mean = mean.add(point);
    }

    return mean.scale(1f / (points.size()));
  }

  @Override
  public boolean equals(Object obj) {
    if (this == obj) {
      return true;
    }
    if (obj == null || getClass() != obj.getClass()) {
      return false;
    }
    Vector3 point = (Vector3) obj;
    return x == point.x && y == point.y && z == point.z;
  }

  @Override
  public int hashCode() {
    return Objects.hash(x, y, z);
  }

  public Vector3 clone() {
    return new Vector3(x, y, z);
  }

  @Override
  public String toString() {
    return "Vector3{" +
      "x=" + x +
      ", y=" + y +
      ", z=" + z +
      '}';
  }
}
