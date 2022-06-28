package sh.ball.engine;

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
  private static final int VERTEX_SAMPLES = 1000;

  public final CameraDrawKernel kernel = new CameraDrawKernel();

  private double focalLength;
  private Vector3 pos;
  private boolean hideEdges = false;
  private boolean usingGpu = false;

  public Camera(double focalLength, Vector3 pos) {
    this.focalLength = focalLength;
    this.pos = pos;
  }

  public Camera(Vector3 pos) {
    this(DEFAULT_FOCAL_LENGTH, pos);
  }

  public List<Shape> draw(WorldObject worldObject) {
    return kernel.draw(this, worldObject);
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
      for (int j = 0; j < Math.min(VERTEX_SAMPLES, clone.numVertices()); j++) {
        vertices.add(project(clone.getVertex(j)));
      }
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
    double focal = focalLength;

    return new Vector2(
      vertex.x * focal / (vertex.z - pos.z) + pos.x,
      vertex.y * focal / (vertex.z - pos.z) + pos.y
    );
  }

  public void setFocalLength(double focalLength) {
    this.focalLength = focalLength;
  }

  public double getFocalLength() {
    return focalLength;
  }

  public void hideEdges(boolean hideEdges) {
    this.hideEdges = hideEdges;
  }

  public boolean edgesHidden() {
    return hideEdges;
  }

  public void usingGpu(boolean usingGpu) {
    this.usingGpu = usingGpu;
  }

  public boolean isUsingGpu() {
    return usingGpu;
  }
}
