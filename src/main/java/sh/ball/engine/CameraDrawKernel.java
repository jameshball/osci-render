package sh.ball.engine;

import com.aparapi.Kernel;
import com.aparapi.Range;
import com.aparapi.exception.QueryFailedException;
import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public class CameraDrawKernel extends Kernel {

  private WorldObject prevObject = null;
  private List<WorldObject> prevObjects = null;
  private float[] vertices;
  private float[] vertexResult;
  private float[] triangles;
  private float[] matrices = new float[1];
  private int[] vertexNums = new int[1];
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
  private int hideEdges = 0;
  private int usingObjectSet = 0;

  public CameraDrawKernel() {}

  private int initialiseVertices(int count, List<List<Vector3>> vertices) {
    for (List<Vector3> vectors : vertices) {
      for (Vector3 vertex : vectors) {
        this.vertices[3 * count] = (float) vertex.x;
        this.vertices[3 * count + 1] = (float) vertex.y;
        this.vertices[3 * count + 2] = (float) vertex.z;
        count++;
      }
      // Set it to NaN so that the line connecting the vertex before and after
      // this path segment is not drawn.
      this.vertices[3 * count] = Float.NaN;
      this.vertices[3 * count + 1] = Float.NaN;
      this.vertices[3 * count + 2] = Float.NaN;
      count++;
    }

    return count;
  }

  public List<Shape> draw(ObjectSet objects, float focalLength) {
    this.focalLength = focalLength;
    usingObjectSet = 1;
    List<WorldObject> objectList = objects.objects.stream().toList();
    if (!objectList.equals(prevObjects)) {
      prevObjects = objectList;
      List<List<List<Vector3>>> vertices = objectList.stream().map(WorldObject::getVertexPath).toList();
      this.vertexNums = vertices.stream().mapToInt(
        l -> l.stream()
          .map(List::size)
          .reduce(0, Integer::sum) + l.size()
      ).toArray();
      int numVertices = Arrays.stream(vertexNums).sum();
      this.vertices = new float[numVertices * 3];
      this.vertexResult = new float[numVertices * 2];
      this.matrices = new float[vertices.size() * 16];
      List<float[]> triangles = objectList.stream().map(WorldObject::getTriangles).toList();
      int numTriangles = triangles.stream().map(arr -> arr.length).reduce(0, Integer::sum);
      this.triangles = new float[numTriangles];
      int offset = 0;
      for (float[] triangleArray : triangles) {
        System.arraycopy(triangleArray, 0, this.triangles, offset, triangleArray.length);
        offset += triangleArray.length;
      }
      int count = 0;
      for (List<List<Vector3>> vertexList : vertices) {
        count = initialiseVertices(count, vertexList);
      }
    }

    int offset = 0;
    for (float[] matrix : objects.cameraSpaceMatrices) {
      System.arraycopy(matrix, 0, this.matrices, offset, matrix.length);
      offset += matrix.length;
    }

    this.cameraPosX = 0;
    this.cameraPosY = 0;
    this.cameraPosZ = 0;

    return executeKernel();
  }

  public List<Shape> draw(Camera camera, WorldObject object) {
    usingObjectSet = 0;
    if (prevObject != object) {
      List<List<Vector3>> vertices = object.getVertexPath();
      int numVertices = vertices.stream().map(List::size).reduce(0, Integer::sum) + vertices.size();
      this.vertices = new float[numVertices * 3];
      this.vertexResult = new float[numVertices * 2];
      this.triangles = object.getTriangles();
      initialiseVertices(0, vertices);
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

    hideEdges = object.edgesHidden() ? 1 : 0;
    return executeKernel();
  }

  private List<Shape> executeKernel() {
    int maxGroupSize = 256;
    try {
      maxGroupSize = getKernelMaxWorkGroupSize(getTargetDevice());
    } catch (QueryFailedException e) {
      e.printStackTrace();
    }

    execute(Range.create(roundUp(vertices.length / 3, maxGroupSize), maxGroupSize));

    List<Shape> linesList = new ArrayList<>();

    for (int i = 0; i < vertices.length / 3; i++) {
      int nextOffset = 0;
      if (i < vertices.length / 3 - 1) {
        nextOffset = 2 * i + 2;
      }
      if (!Float.isNaN(vertexResult[2 * i]) && !Float.isNaN(vertexResult[nextOffset])) {
        linesList.add(new Line(
          new Vector2(vertexResult[2 * i], vertexResult[2 * i + 1]),
          new Vector2(vertexResult[nextOffset], vertexResult[nextOffset + 1])
        ));
      }
    }

    return linesList;
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
    float EPSILON = 0.00001f;

    float x1 = vertices[3 * i];
    float y1 = vertices[3 * i + 1];
    float z1 = vertices[3 * i + 2];

    // Check for NaN and return NaN projected vertices immediately
    if (x1 != x1 || y1 != y1 || z1 != z1) {
      vertexResult[2 * i] = Float.NaN;
      vertexResult[2 * i + 1] = Float.NaN;
      return;
    }

    // Initialisation required otherwise aparapi doesn't work (bug)
    float cosValue = 0, sinValue = 0, x2 = 0, x3 = 0, y2 = 0, y3 = 0, z2 = 0, z3 = 0, rotatedX = 0, rotatedY = 0, rotatedZ = 0;

    if (usingObjectSet == 0) {
      // rotate around x-axis
      cosValue = cos(rotationX);
      sinValue = sin(rotationX);
      y2 = cosValue * y1 - sinValue * z1;
      z2 = sinValue * y1 + cosValue * z1;

      // rotate around y-axis
      cosValue = cos(rotationY);
      sinValue = sin(rotationY);
      x2 = cosValue * x1 + sinValue * z2;
      z3 = -sinValue * x1 + cosValue * z2;

      // rotate around z-axis
      cosValue = cos(rotationZ);
      sinValue = sin(rotationZ);
      x3 = cosValue * x2 - sinValue * y2;
      y3 = sinValue * x2 + cosValue * y2;

      // add position
      rotatedX = x3 + positionX;
      rotatedY = y3 + positionY;
      rotatedZ = z3 + positionZ;
    } else {
      int totalVertices = 0;
      int obj = -1;
      for (int j = 0; j < vertexNums.length; j++) {
        totalVertices += vertexNums[j];
        if (totalVertices >= i && obj == -1) {
          obj = j;
        }
      }
      rotatedX = matrices[16 * obj] * x1 + matrices[16 * obj + 1] * y1 + matrices[16 * obj + 2] * z1 + matrices[16 * obj + 3];
      rotatedY = matrices[16 * obj + 4] * x1 + matrices[16 * obj + 5] * y1 + matrices[16 * obj + 6] * z1 + matrices[16 * obj + 7];
      rotatedZ = matrices[16 * obj + 8] * x1 + matrices[16 * obj + 9] * y1 + matrices[16 * obj + 10] * z1 + matrices[16 * obj + 11];
      // Also need to consider homogeneous coordinates here
    }

    boolean intersects = false;

    if (hideEdges == 1) {
      float rotatedCameraPosX = cameraPosX - positionX;
      float rotatedCameraPosY = cameraPosY - positionY;
      float rotatedCameraPosZ = cameraPosZ - positionZ;

      cosValue = cos(-rotationZ);
      sinValue = sin(-rotationZ);
      x2 = cosValue * rotatedCameraPosX - sinValue * rotatedCameraPosY;
      y2 = sinValue * rotatedCameraPosX + cosValue * rotatedCameraPosY;

      cosValue = cos(-rotationY);
      sinValue = sin(-rotationY);
      rotatedCameraPosX = cosValue * x2 + sinValue * rotatedCameraPosZ;
      z2 = -sinValue * x2 + cosValue * rotatedCameraPosZ;

      cosValue = cos(-rotationX);
      sinValue = sin(-rotationX);
      rotatedCameraPosY = cosValue * y2 - sinValue * z2;
      rotatedCameraPosZ = sinValue * y2 + cosValue * z2;

      float dirx = rotatedCameraPosX - x1;
      float diry = rotatedCameraPosY - y1;
      float dirz = rotatedCameraPosZ - z1;
      float length = sqrt(dirx * dirx + diry * diry + dirz * dirz);
      dirx /= length;
      diry /= length;
      dirz /= length;

      for (int j = 0; j < triangles.length / 9 && !intersects; j++) {
        float v1x = triangles[9 * j];
        float v1y = triangles[9 * j + 1];
        float v1z = triangles[9 * j + 2];
        float v2x = triangles[9 * j + 3];
        float v2y = triangles[9 * j + 4];
        float v2z = triangles[9 * j + 5];
        float v3x = triangles[9 * j + 6];
        float v3y = triangles[9 * j + 7];
        float v3z = triangles[9 * j + 8];

        // vec3 edge1 = v2 - v1;
        float edge1x = v2x - v1x;
        float edge1y = v2y - v1y;
        float edge1z = v2z - v1z;

        // vec3 edge2 = v3 - v1;
        float edge2x = v3x - v1x;
        float edge2y = v3y - v1y;
        float edge2z = v3z - v1z;

        // vec3 p = cross(dir, edge2);
        float px = crossX(dirx, diry, dirz, edge2x, edge2y, edge2z);
        float py = crossY(dirx, diry, dirz, edge2x, edge2y, edge2z);
        float pz = crossZ(dirx, diry, dirz, edge2x, edge2y, edge2z);

        float det = dot(edge1x, edge1y, edge1z, px, py, pz);

        if (det > -EPSILON && det < EPSILON) {
          continue;
        }

        float inv_det = 1.0f / det;

        // vec3 t = origin - v1;
        float tx = x1 - v1x;
        float ty = y1 - v1y;
        float tz = z1 - v1z;

        float u = dot(tx, ty, tz, px, py, pz) * inv_det;

        if (u < 0 || u > 1) {
          continue;
        }

        // vec3 q = cross(t, edge1);
        float qx = crossX(tx, ty, tz, edge1x, edge1y, edge1z);
        float qy = crossY(tx, ty, tz, edge1x, edge1y, edge1z);
        float qz = crossZ(tx, ty, tz, edge1x, edge1y, edge1z);

        float v = dot(dirx, diry, dirz, qx, qy, qz) * inv_det;

        if (v < 0 || u + v > 1) {
          continue;
        }

        float mu = dot(edge2x, edge2y, edge2z, qx, qy, qz) * inv_det;

        if (mu > EPSILON) {
          intersects = true;
        }
      }
    }

    if (intersects) {
      vertexResult[2 * i] = Float.NaN;
      vertexResult[2 * i + 1] = Float.NaN;
    } else {
      // projection
      vertexResult[2 * i] = rotatedX * focalLength / (rotatedZ - cameraPosZ) + cameraPosX;
      vertexResult[2 * i + 1] = rotatedY * focalLength / (rotatedZ - cameraPosZ) + cameraPosY;
    }
  }

  float crossX(float x0, float x1, float x2, float y0, float y1, float y2) {
    return x1 * y2 - y1 * x2;
  }

  float crossY(float x0, float x1, float x2, float y0, float y1, float y2) {
    return x2 * y0 - y2 * x0;
  }

  float crossZ(float x0, float x1, float x2, float y0, float y1, float y2) {
    return x0 * y1 - y0 * x1;
  }

  float dot(float x0, float x1, float x2, float y0, float y1, float y2) {
    return x0 * y0 + x1 * y1 + x2 * y2;
  }
}
