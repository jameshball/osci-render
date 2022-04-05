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
  private float[] vertices;
  private float[] vertexResult;
  private List<Shape> linesList;
  private float[] triangles;
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

  public CameraDrawKernel() {}

  public List<Shape> draw(Camera camera, WorldObject object) {
    if (prevObject != object) {
      List<Vector3> vertices = object.getVertexPath();
      this.vertices = new float[vertices.size() * 3];
      this.vertexResult = new float[vertices.size() * 2];
      this.triangles = object.getTriangles();
      for (int i = 0; i < vertices.size(); i++) {
        Vector3 vertex = vertices.get(i);
        this.vertices[3 * i] = (float) vertex.x;
        this.vertices[3 * i + 1] = (float) vertex.y;
        this.vertices[3 * i + 2] = (float) vertex.z;
      }
    }
    prevObject = object;
    hideEdges = object.edgesHidden() ? 1 : 0;
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

    linesList = new ArrayList<>();

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

    // rotate around x-axis
    float cosValue = cos(rotationX);
    float sinValue = sin(rotationX);
    float y2 = cosValue * y1 - sinValue * z1;
    float z2 = sinValue * y1 + cosValue * z1;

    // rotate around y-axis
    cosValue = cos(rotationY);
    sinValue = sin(rotationY);
    float x2 = cosValue * x1 + sinValue * z2;
    float z3 = -sinValue * x1 + cosValue * z2;

    // rotate around z-axis
    cosValue = cos(rotationZ);
    sinValue = sin(rotationZ);
    float x3 = cosValue * x2 - sinValue * y2;
    float y3 = sinValue * x2 + cosValue * y2;

    // add position
    float rotatedX = x3 + positionX;
    float rotatedY = y3 + positionY;
    float rotatedZ = z3 + positionZ;

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
