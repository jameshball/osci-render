package sh.ball.engine;

import com.aparapi.Kernel;
import com.aparapi.Range;
import com.aparapi.exception.QueryFailedException;
import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.Arrays;
import java.util.List;

public class CameraDrawKernel extends Kernel {

  private WorldObject prevObject = null;
  private float[] vertices;
  private float[] vertexResult;
  private Shape[] lines;
  private float rotationX;
  private float rotationY;
  private float rotationZ;
  private float positionX;
  private float positionY;
  private float positionZ;
  private float cameraPosX;
  private float cameraPosY;
  private float cameraPosZ;
  private float focalLength;

  public CameraDrawKernel() {}

  public List<Shape> draw(Camera camera, WorldObject object) {
    if (prevObject != object) {
      List<Vector3> vertices = object.getVertexPath();
      this.vertices = new float[vertices.size() * 3];
      this.vertexResult = new float[vertices.size() * 2];
      this.lines = new Line[vertices.size() / 2];
      for (int i = 0; i < vertices.size(); i++) {
        Vector3 vertex = vertices.get(i);
        this.vertices[3 * i] = (float) vertex.x;
        this.vertices[3 * i + 1] = (float) vertex.y;
        this.vertices[3 * i + 2] = (float) vertex.z;
      }
    }
    prevObject = object;
    Vector3 rotation = object.getRotation();
    Vector3 position = object.getPosition();
    this.rotationX = (float) rotation.x;
    this.rotationY = (float) rotation.y;
    this.rotationZ = (float) rotation.z;
    this.positionX = (float) position.x;
    this.positionY = (float) position.y;
    this.positionZ = (float) position.z;
    Vector3 cameraPos = camera.getPos();
    this.cameraPosX = (float) cameraPos.x;
    this.cameraPosY = (float) cameraPos.y;
    this.cameraPosZ = (float) cameraPos.z;
    this.focalLength = (float) camera.getFocalLength();

    int maxGroupSize = 256;
    try {
      maxGroupSize = getKernelMaxWorkGroupSize(getTargetDevice());
    } catch (QueryFailedException e) {
      e.printStackTrace();
    }

    execute(Range.create(roundUp(vertices.length / 3, maxGroupSize), maxGroupSize));

    for (int i = 0; i < vertices.length / 3; i += 2) {
      lines[i / 2] = new Line(
        new Vector2(vertexResult[2 * i], vertexResult[2 * i + 1]),
        new Vector2(vertexResult[2 * i + 2], vertexResult[2 * i + 3])
      );
    }

    return Arrays.asList(lines);
  }

  int roundUp(int round, int multiple) {
    if (multiple == 0) {
      return round;
    }

    int remainder = round % multiple;
    if (remainder == 0) {
      return round;
    }

    return round + multiple - remainder;
  }

  @Override
  public void run() {
    int i = getGlobalId();
    // get vertex
    float x = vertices[3 * i];
    float y = vertices[3 * i + 1];
    float z = vertices[3 * i + 2];

    // rotate around x-axis
    float cosValue = cos(rotationX);
    float sinValue = sin(rotationX);
    float y2 = cosValue * y - sinValue * z;
    float z2 = sinValue * y + cosValue * z;

    // rotate around y-axis
    cosValue = cos(rotationY);
    sinValue = sin(rotationY);
    float x2 = cosValue * x + sinValue * z2;
    float z3 = -sinValue * x + cosValue * z2;

    // rotate around z-axis
    cosValue = cos(rotationZ);
    sinValue = sin(rotationZ);
    float x3 = cosValue * x2 - sinValue * y2;
    float y3 = sinValue * x2 + cosValue * y2;

    // add position
    x = x3 + positionX;
    y = y3 + positionY;
    z = z3 + positionZ;

    // projection
    vertexResult[2 * i] = x * focalLength / (z - cameraPosZ) + cameraPosX;
    vertexResult[2 * i + 1] = y * focalLength / (z - cameraPosZ) + cameraPosY;
  }
}
