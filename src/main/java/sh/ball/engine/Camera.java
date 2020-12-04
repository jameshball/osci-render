package sh.ball.engine;

import java.util.HashMap;
import java.util.Map;
import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class Camera {

  private static final int MAX_NUM_STEPS = 1000;

  public static double DEFAULT_FOCAL_LENGTH = 1;

  // Threshold for the max vertex value being displayed when rendering (will change position of
  // camera to scale the image)
  private static final double VERTEX_VALUE_THRESHOLD = 1;
  private static final double CAMERA_MOVE_INCREMENT = -0.1;
  private static final int SAMPLE_RENDER_SAMPLES = 50;
  private static final double EPSILON = 0.001;

  private double focalLength;

  private Vector3 pos;

  public Camera(double focalLength, Vector3 pos) {
    this.focalLength = focalLength;
    this.pos = pos;
  }

  public Camera(Vector3 pos) {
    this(DEFAULT_FOCAL_LENGTH, pos);
  }

  public List<Shape> draw(WorldObject worldObject) {
    return getFrame(getProjectedVertices(worldObject), worldObject.getVertexPath());
  }

  public Map<Vector3, Vector2> getProjectedVertices(WorldObject worldObject) {
    Map<Vector3, Vector2> projectionMap = new HashMap<>();

    for (Vector3 vertex : worldObject.getVertices()) {
      projectionMap.put(vertex, project(vertex));
    }

    return projectionMap;
  }

  // Automatically finds the correct Z position to use to view the world object properly.
  public void findZPos(WorldObject object) {
    setPos(new Vector3());
    List<Vector2> vertices = new ArrayList<>();

    int stepsMade = 0;
    while (maxVertexValue(vertices) > VERTEX_VALUE_THRESHOLD && stepsMade < MAX_NUM_STEPS) {
      move(new Vector3(0, 0, CAMERA_MOVE_INCREMENT));
      vertices = sampleVerticesInRender(object);
      stepsMade++;
    }
  }

  // Does a 'sample render' to find the possible range of projected vectors on the screen to reduce
  // clipping on edges of the oscilloscope screen.
  private List<Vector2> sampleVerticesInRender(WorldObject object) {
    Vector3 rotation = new Vector3(0, 2 * Math.PI / SAMPLE_RENDER_SAMPLES,
        2 * Math.PI / SAMPLE_RENDER_SAMPLES);
    WorldObject clone = object.clone();
    List<Vector2> vertices = new ArrayList<>();

    for (int i = 0; i < SAMPLE_RENDER_SAMPLES - 1; i++) {
      vertices.addAll(getProjectedVertices(clone).values());
      clone.rotate(rotation);
    }

    return vertices;
  }

  private static double maxVertexValue(List<Vector2> vertices) {
    return vertices
        .stream()
        .map((vec) -> Math.max(Math.abs(vec.getX()), Math.abs(vec.getY())))
        .max(Double::compareTo)
        .orElse(Double.POSITIVE_INFINITY);
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
    if (vertex.getZ() - pos.getZ() < EPSILON) {
      return new Vector2();
    }

    double oldFocalLength = focalLength;

    return new Vector2(
        vertex.getX() * oldFocalLength / (vertex.getZ() - pos.getZ()) + pos.getX(),
        vertex.getY() * oldFocalLength / (vertex.getZ() - pos.getZ()) + pos.getY()
    );
  }

  public List<Shape> getFrame(Map<Vector3, Vector2> projectionMap, List<Vector3> vertexPath) {
    List<Shape> lines = new ArrayList<>();

    for (int i = 0; i < vertexPath.size(); i += 2) {
      lines.add(new Line(
          projectionMap.get(vertexPath.get(i)),
          projectionMap.get(vertexPath.get(i + 1))
      ));
    }

    return lines;
  }

  public void setFocalLength(double focalLength) {
    this.focalLength = focalLength;
  }
}
