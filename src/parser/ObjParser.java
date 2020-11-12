package parser;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import shapes.Shape;

public class ObjParser extends FileParser {

  private static double OBJ_ROTATE_SPEED = Math.PI / 1000;

  private List<List<Shape>> shapes;

  private final Vector3 cameraPos;
  private final Vector3 rotation;
  private final double focalLength;
  private final boolean isDefaultPosition;

  private WorldObject object;
  private Camera camera;

  public ObjParser(String path, float rotateSpeed, float cameraX, float cameraY, float cameraZ,
      float focalLength, boolean isDefaultPosition) throws IOException {
    ObjParser.OBJ_ROTATE_SPEED *= rotateSpeed;
    checkFileExtension(path);
    this.shapes = new ArrayList<>();
    this.cameraPos = new Vector3(cameraX, cameraY, cameraZ);
    this.isDefaultPosition = isDefaultPosition;
    this.focalLength = focalLength;
    this.rotation = new Vector3(0, OBJ_ROTATE_SPEED, OBJ_ROTATE_SPEED);
    parseFile(path);
  }

  @Override
  protected String getFileExtension() {
    return "obj";
  }

  @Override
  protected void parseFile(String path) throws IllegalArgumentException, IOException {
    object = new WorldObject(path);
    camera = new Camera(focalLength, cameraPos);
    // If camera position arguments haven't been specified, automatically work out the position of
    // the camera based on the size of the object in the camera's view.
    if (isDefaultPosition) {
      camera.findZPos(object);
    }
  }

  @Override
  public List<Shape> nextFrame() {
    object.rotate(rotation);
    return camera.draw(object);
  }
}
