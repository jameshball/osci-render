package engine;

import shapes.Line;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class Camera {

  public static double DEFAULT_FOCAL_LENGTH = 1;

  private double focalLength;
  private double clipping = 0.001;
  private Vector3 pos;

  public Camera(double focalLength, Vector3 pos) {
    this.focalLength = focalLength;
    this.pos = pos;
  }

  public Camera(double focalLength) {
    this(focalLength, new Vector3());
  }

  public Camera(Vector3 pos) {
    this(DEFAULT_FOCAL_LENGTH, pos);
  }

  public Camera() {
    this(DEFAULT_FOCAL_LENGTH, new Vector3());
  }

  public List<Line> draw(WorldObject worldObject) {
    return getFrame(getProjectedVertices(worldObject), worldObject.getEdgeData());
  }

  public List<Vector2> getProjectedVertices(WorldObject worldObject) {
    List<Vector2> vertices = new ArrayList<>();

    for (Vector3 vertex : worldObject.getVertices()) {
      vertices.add(project(vertex));
    }

    return vertices;
  }

  public Vector3 getPos() {
    return pos.clone();
  }

  public void setPos(Vector3 pos) {
    this.pos = pos;
  }

  public void move(Vector3 dir) {
    pos = pos.add(dir);
  }

  private Vector2 project(Vector3 vertex) {
    if (vertex.getZ() - pos.getZ() < clipping) {
      return new Vector2();
    }

    return new Vector2(
        vertex.getX() * focalLength / (vertex.getZ() - pos.getZ()) + pos.getX(),
        vertex.getY() * focalLength / (vertex.getZ() - pos.getZ()) + pos.getY()
    );
  }

  public List<Line> getFrame(List<Vector2> vertices, List<Integer> connections) {
    List<Line> lines = new ArrayList<>();

    for (int i = 0; i < connections.size(); i += 2) {
      Vector2 start = vertices.get(Math.abs(connections.get(i)));
      Vector2 end = vertices.get(Math.abs(connections.get(i + 1)));
      double x1 = start.getX();
      double y1 = start.getY();
      double x2 = end.getX();
      double y2 = end.getY();

      lines.add(new Line(x1, y1, x2, y2));
    }

    return lines;
  }
}
