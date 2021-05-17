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
      getX() + other.getX(),
      getY() + other.getY(),
      getZ() + other.getZ()
    );
  }

  public Vector3 scale(double factor) {
    return new Vector3(
      getX() * factor,
      getY() * factor,
      getZ() * factor
    );
  }

  public Vector3 rotate(Vector3 rotation) {
    return rotateX(rotation.getX())
      .rotateY(rotation.getY())
      .rotateZ(rotation.getZ());
  }

  public Vector3 rotateX(double theta) {
    return new Vector3(
      getX(),
      Math.cos(theta) * getY() - Math.sin(theta) * getZ(),
      Math.sin(theta) * getY() + Math.cos(theta) * getZ()
    );
  }

  public Vector3 rotateY(double theta) {
    return new Vector3(
      Math.cos(theta) * getX() + Math.sin(theta) * getZ(),
      getY(),
      -Math.sin(theta) * getX() + Math.cos(theta) * getZ()
    );
  }

  public Vector3 rotateZ(double theta) {
    return new Vector3(
      Math.cos(theta) * getX() - Math.sin(theta) * getY(),
      Math.sin(theta) * getX() + Math.cos(theta) * getY(),
      getZ()
    );
  }

  // TODO: Is this correctly used?!
  public double distance(Vector3 vector) {
    return Math.sqrt(Math.pow(vector.x, 2) + Math.pow(vector.y, 2) + Math.pow(vector.z, 2));
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
