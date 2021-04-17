package sh.ball.parser.obj;

import sh.ball.FrameSet;
import sh.ball.engine.Camera;
import sh.ball.engine.Vector3;
import sh.ball.engine.WorldObject;

import java.io.IOException;
import java.util.List;

import sh.ball.parser.FileParser;
import sh.ball.shapes.Shape;

public class ObjParser extends FileParser<FrameSet<List<Shape>>> {

  private static final float DEFAULT_ROTATE_SPEED = 3;

  private final Vector3 rotation;
  private final boolean isDefaultPosition;
  private final String filePath;
  private final Camera camera;

  private WorldObject object;

  public ObjParser(String path, float rotateSpeed, float cameraX, float cameraY, float cameraZ,
                   float focalLength, boolean isDefaultPosition) {
    rotateSpeed *= Math.PI / 1000;
    checkFileExtension(path);
    this.filePath = path;
    this.isDefaultPosition = isDefaultPosition;
    Vector3 cameraPos = new Vector3(cameraX, cameraY, cameraZ);
    this.camera = new Camera(focalLength, cameraPos);
    this.rotation = new Vector3(0, rotateSpeed, rotateSpeed);
  }

  public ObjParser(String path, float focalLength) {
    this(path, DEFAULT_ROTATE_SPEED, 0, 0, 0, focalLength, true);
  }

  public ObjParser(String path) {
    this(path, (float) Camera.DEFAULT_FOCAL_LENGTH);
  }

  @Override
  public String getFileExtension() {
    return "obj";
  }

  @Override
  public FrameSet<List<Shape>> parse() throws IllegalArgumentException, IOException {
    object = new WorldObject(filePath);

    if (isDefaultPosition) {
      camera.findZPos(object);
    }

    return new ObjFrameSet(object, camera, rotation, isDefaultPosition);
  }

  @Override
  public String getFilePath() {
    return filePath;
  }

  // If camera position arguments haven't been specified, automatically work out the position of
  // the camera based on the size of the object in the camera's view.
  public void setFocalLength(double focalLength) {
    camera.setFocalLength(focalLength);
    if (isDefaultPosition) {
      camera.findZPos(object);
    }
  }

  public static boolean isObjFile(String path) {
    return path.matches(".*\\.obj");
  }

  public void setCameraPos(Vector3 vector) {
    camera.setPos(vector);
  }
}
