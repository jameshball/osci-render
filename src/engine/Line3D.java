package engine;

import java.util.Objects;

public class Line3D {

  private final Vector3 start;
  private final Vector3 end;

  public Line3D(Vector3 start, Vector3 end) {
    this.start = start;
    this.end = end;
  }

  public Vector3 getStart() {
    return start;
  }

  public Vector3 getEnd() {
    return end;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) {
      return true;
    }
    if (o == null || getClass() != o.getClass()) {
      return false;
    }
    Line3D line3D = (Line3D) o;
    return Objects.equals(start, line3D.start) && Objects.equals(end, line3D.end);
  }

  @Override
  public int hashCode() {
    return Objects.hash(start, end);
  }

  @Override
  public String toString() {
    return "Line3D{" +
        "start=" + start +
        ", end=" + end +
        '}';
  }
}
