package engine;

import shapes.Line;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class Camera {

  public static double DEFAULT_FOCAL_LENGTH = 1;

  // Threshold for the max vertex value being displayed when rendering (will change position of
  // camera to scale the image)
  private static final double VERTEX_VALUE_THRESHOLD = 1;
  private static final double CAMERA_MOVE_INCREMENT = -0.1;
  private static final int SAMPLE_RENDER_SAMPLES = 50;
  private static final double EPSILON = 0.001;

  private final double focalLength;

  private Vector3 pos;

  public Camera(double focalLength, Vector3 pos) {
    this.focalLength = focalLength;
    this.pos = pos;
  }

  public Camera(double focalLength, WorldObject object) {
    this(focalLength, new Vector3());
    findZPos(object);
  }

  public Camera(Vector3 pos) {
    this(DEFAULT_FOCAL_LENGTH, pos);
  }

  public Camera(WorldObject object) {
    this(DEFAULT_FOCAL_LENGTH, object);
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

  // Automatically finds the correct Z position to use to view the world object properly.
  public void findZPos(WorldObject object) {
    setPos(new Vector3());
    List<Vector2> vertices = new ArrayList<>();

    while (maxVertexValue(vertices) > VERTEX_VALUE_THRESHOLD) {
      move(new Vector3(0, 0, CAMERA_MOVE_INCREMENT));
      vertices = sampleVerticesInRender(object);
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
      vertices.addAll(getProjectedVertices(clone));
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
