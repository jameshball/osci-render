package audio;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.IntStream;
import shapes.Shape;
import shapes.Shapes;
import shapes.Vector2;

public class AudioClient {

  private static final int SAMPLE_RATE = 192000;
  private static double OBJ_ROTATE_SPEED = Math.PI / 1000;
  private static final float ROTATE_SPEED = 0;
  private static final float TRANSLATION_SPEED = 0;
  private static final Vector2 TRANSLATION = new Vector2(0.3, 0.3);
  private static final float SCALE = 1;
  private static final float WEIGHT = Shape.DEFAULT_WEIGHT;

  // args:
  // args[0] - path of .obj file
  // args[1] - focal length of camera
  // args[2] - x position of camera
  // args[3] - y position of camera
  // args[4] - z position of camera
  // args[5] - rotation speed of object
  //
  // example:
  // osci-render models/cube.obj 1 0 0 -3 10
  public static void main(String[] programArgs) {
    // TODO: Calculate weight of lines using depth.
    //  Reduce weight of lines drawn multiple times.
    //  Find intersections of lines to (possibly) improve line cleanup.
    //  Improve performance of line cleanup with a heuristic.

    AudioArgs args = new AudioArgs(programArgs);

    OBJ_ROTATE_SPEED *= args.rotateSpeed();

    Vector3 cameraPos = new Vector3(args.cameraX(), args.cameraY(), args.cameraZ());
    WorldObject object = new WorldObject(args.objFilePath());

    System.out.println(args.isDefaultPosition());

    Camera camera = args.isDefaultPosition() ? new Camera(args.focalLength(), object)
        : new Camera(args.focalLength(), cameraPos);

    Vector3 rotation = new Vector3(0, OBJ_ROTATE_SPEED, OBJ_ROTATE_SPEED);

    System.out.println("Begin pre-render...");
    List<List<? extends Shape>> frames = preRender(object, rotation, camera);
    System.out.println("Finish pre-render");
    System.out.println("Connecting to audio player");
    AudioPlayer player = new AudioPlayer(SAMPLE_RATE, frames, ROTATE_SPEED, TRANSLATION_SPEED,
        TRANSLATION, SCALE, WEIGHT);
    System.out.println("Starting audio stream");
    player.play();
  }

  private static List<List<? extends Shape>> preRender(WorldObject object, Vector3 rotation,
      Camera camera) {
    List<List<? extends Shape>> preRenderedFrames = new ArrayList<>();
    int numFrames = (int) (2 * Math.PI / OBJ_ROTATE_SPEED);

    for (int i = 0; i < numFrames; i++) {
      preRenderedFrames.add(new ArrayList<>());
    }

    AtomicInteger renderedFrames = new AtomicInteger();

    // pre-renders the WorldObject in parallel
    IntStream.range(0, numFrames).parallel().forEach((frameNum) -> {
      WorldObject clone = object.clone();
      clone.rotate(rotation.scale(frameNum));
      preRenderedFrames.set(frameNum, Shapes.sortLines(camera.draw(clone)));
      int numRendered = renderedFrames.getAndIncrement();

      if (numRendered % 50 == 0) {
        System.out.println("Rendered " + numRendered + " frames of " + (numFrames + 1) + " total");
      }
    });

    return preRenderedFrames;
  }
}