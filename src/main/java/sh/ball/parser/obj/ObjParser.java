package sh.ball.parser.obj;

import sh.ball.audio.FrameSource;
import sh.ball.engine.Camera;
import sh.ball.engine.Vector3;
import sh.ball.engine.WorldObject;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.List;

import sh.ball.parser.FileParser;
import sh.ball.shapes.Shape;

public class ObjParser extends FileParser<FrameSource<List<Shape>>> {

  private final boolean isDefaultPosition;
  private final InputStream input;
  private final Camera camera;

  private WorldObject object;

  public ObjParser(InputStream input, float cameraX, float cameraY, float cameraZ,
                   float focalLength, boolean isDefaultPosition) {
    this.input = input;
    this.isDefaultPosition = isDefaultPosition;
    Vector3 cameraPos = new Vector3(cameraX, cameraY, cameraZ);
    this.camera = new Camera(focalLength, cameraPos);
  }

  public ObjParser(InputStream input, float focalLength) {
    this(input, 0, 0, 0, focalLength, true);
  }

  public ObjParser(InputStream input) {
    this(input, (float) Camera.DEFAULT_FOCAL_LENGTH);
  }

  public ObjParser(String path) throws FileNotFoundException {
    this(new FileInputStream(path), (float) Camera.DEFAULT_FOCAL_LENGTH);
  }

  @Override
  public String getFileExtension() {
    return "obj";
  }

  @Override
  public FrameSource<List<Shape>> parse() throws IllegalArgumentException, IOException {
    object = new WorldObject(input);
    camera.findZPos(object);

    return new ObjFrameSource(object, camera);
  }

  @Override
  public FrameSource<List<Shape>> get() throws IOException {
    return parse();
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
}
