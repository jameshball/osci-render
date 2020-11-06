package parser;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.IntStream;
import shapes.Shape;
import shapes.Shapes;

public class ObjParser extends FileParser {

  private static double OBJ_ROTATE_SPEED = Math.PI / 1000;

  private List<List<Shape>> shapes;

  private final float rotateSpeed;
  private final float cameraX;
  private final float cameraY;
  private final float cameraZ;
  private final float focalLength;
  private final boolean isDefaultPosition;

  public ObjParser(String path, float rotateSpeed, float cameraX, float cameraY, float cameraZ,
      float focalLength, boolean isDefaultPosition) throws IOException {
    checkFileExtension(path);
    shapes = new ArrayList<>();
    this.rotateSpeed = rotateSpeed;
    this.cameraX = cameraX;
    this.cameraY = cameraY;
    this.cameraZ = cameraZ;
    this.focalLength = focalLength;
    this.isDefaultPosition = isDefaultPosition;
    parseFile(path);
  }

  @Override
  protected String getFileExtension() {
    return "obj";
  }

  @Override
  protected void parseFile(String path) throws IllegalArgumentException, IOException {
    OBJ_ROTATE_SPEED *= rotateSpeed;

    Vector3 cameraPos = new Vector3(cameraX, cameraY, cameraZ);
    WorldObject object = new WorldObject(path);

    // If camera position arguments haven't been specified, automatically work out the position of
    // the camera based on the size of the object in the camera's view.
    Camera camera = isDefaultPosition ? new Camera(focalLength, object)
        : new Camera(focalLength, cameraPos);

    Vector3 rotation = new Vector3(0, OBJ_ROTATE_SPEED, OBJ_ROTATE_SPEED);

    shapes = preRender(object, rotation, camera);
  }

  @Override
  public List<List<Shape>> getShapes() {
    return shapes;
  }

  private static List<List<Shape>> preRender(WorldObject object, Vector3 rotation,
      Camera camera) {
    List<List<Shape>> preRenderedFrames = new ArrayList<>();
    // Number of frames it will take to render a full rotation of the object.
    int numFrames = (int) (2 * Math.PI / OBJ_ROTATE_SPEED);

    for (int i = 0; i < numFrames; i++) {
      preRenderedFrames.add(new ArrayList<>());
    }

    AtomicInteger renderedFrames = new AtomicInteger();

    // pre-renders the WorldObject in parallel
    IntStream.range(0, numFrames).parallel().forEach((frameNum) -> {
      WorldObject clone = object.clone();
      clone.rotate(rotation.scale(frameNum));
      // Finds all lines to draw the object from the camera's view and then 'sorts' them by finding
      // a hamiltonian path, which dramatically helps with rendering a clean image.
      preRenderedFrames.set(frameNum, Shapes.sortLines(camera.draw(clone)));
      int numRendered = renderedFrames.getAndIncrement();

      if (numRendered % 50 == 0) {
        System.out.println("Rendered " + numRendered + " frames of " + (numFrames + 1) + " total");
      }
    });

    return preRenderedFrames;
  }
}
