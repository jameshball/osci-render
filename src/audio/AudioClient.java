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
  private static final float WEIGHT = 80;

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
  public static void main(String[] args) {
    // TODO: Calculate weight of lines using depth.
    //  Reduce weight of lines drawn multiple times.
    //  Find intersections of lines to (possibly) improve line cleanup.
    //  Improve performance of line cleanup with a heuristic.

    String objFilePath = args[0];
    float focalLength = Float.parseFloat(args[1]);
    float cameraX = Float.parseFloat(args[2]);
    float cameraY = Float.parseFloat(args[3]);
    float cameraZ = Float.parseFloat(args[4]);

    OBJ_ROTATE_SPEED *= Double.parseDouble(args[5]);

    int numFrames = (int) (2 * Math.PI / OBJ_ROTATE_SPEED);

    Camera camera = new Camera(focalLength, new Vector3(cameraX, cameraY, cameraZ));
    WorldObject object = new WorldObject(objFilePath);
    Vector3 rotation = new Vector3(0, OBJ_ROTATE_SPEED, OBJ_ROTATE_SPEED);
    List<List<? extends Shape>> preRenderedFrames = new ArrayList<>();

    for (int i = 0; i < numFrames; i++) {
      preRenderedFrames.add(new ArrayList<>());
    }

    System.out.println("Begin pre-render...");
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

    System.out.println("Finish pre-render");

    System.out.println("Connecting to audio player");
    AudioPlayer player = new AudioPlayer(SAMPLE_RATE, preRenderedFrames);
    player.setRotateSpeed(ROTATE_SPEED);
    player.setTranslation(TRANSLATION_SPEED, TRANSLATION);
    player.setScale(SCALE);
    player.setWeight(WEIGHT);

    System.out.println("Starting audio stream");
    player.start();
  }
}