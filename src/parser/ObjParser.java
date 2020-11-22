package parser;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import java.io.IOException;
import java.util.List;
import shapes.Shape;

public class ObjParser extends FileParser {

  private static final float DEFAULT_ROTATE_SPEED = 3;

  private final Vector3 cameraPos;
  private final Vector3 rotation;
  private final boolean isDefaultPosition;
  private final String filePath;

  private WorldObject object;
  private Camera camera;
  private double focalLength;

  public ObjParser(String path, float rotateSpeed, float cameraX, float cameraY, float cameraZ,
      float focalLength, boolean isDefaultPosition) throws IOException {
    rotateSpeed *= Math.PI / 1000;
    checkFileExtension(path);
    this.filePath = path;
    this.cameraPos = new Vector3(cameraX, cameraY, cameraZ);
    this.isDefaultPosition = isDefaultPosition;
    this.focalLength = focalLength;
    this.rotation = new Vector3(0, rotateSpeed, rotateSpeed);
    parseFile(path);
  }

  public ObjParser(String path, float focalLength) throws IOException {
    this(path, DEFAULT_ROTATE_SPEED, 0, 0, 0, focalLength, true);
  }

  @Override
  protected String getFileExtension() {
    return "obj";
  }

  @Override
  protected void parseFile(String path) throws IllegalArgumentException, IOException {
    object = new WorldObject(path);
    camera = new Camera(focalLength, cameraPos);

    if (isDefaultPosition) {
      camera.findZPos(object);
    }
  }

  @Override
  public List<Shape> nextFrame() {
    object.rotate(rotation);
    return camera.draw(object);
  }

  @Override
  public String getFilePath() {
    return filePath;
  }

  // If camera position arguments haven't been specified, automatically work out the position of
  // the camera based on the size of the object in the camera's view.
  public void setFocalLength(float focalLength) {
    camera.setFocalLength(focalLength);
    if (isDefaultPosition) {
      camera.findZPos(object);
    }
  }
}
