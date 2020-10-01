package audio;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import java.util.ArrayList;
import java.util.List;
import java.util.stream.IntStream;
import shapes.Shape;
import shapes.Shapes;
import shapes.Vector2;

public class AudioClient {
  public static final double TARGET_FRAMERATE = 30;

  private static final int SAMPLE_RATE = 192000;
  private static final double OBJ_ROTATE = Math.PI / 100;
  private static final float ROTATE_SPEED = 0;
  private static final float TRANSLATION_SPEED = 0;
  private static final Vector2 TRANSLATION = new Vector2(1, 1);
  private static final float SCALE = 1;
  private static final float WEIGHT = 80;

  public static void main(String[] args) {
    // TODO: Calculate weight of lines using depth.
    //  Reduce weight of lines drawn multiple times.
    //  Find intersections of lines to (possibly) improve line cleanup.
    //  Improve performance of line cleanup with a heuristic.

    String objFilePath = args[0];
    int numFrames = (int) (Float.parseFloat(args[1]) * TARGET_FRAMERATE);
    float focalLength = Float.parseFloat(args[2]);
    float cameraX = Float.parseFloat(args[3]);
    float cameraY = Float.parseFloat(args[4]);
    float cameraZ = Float.parseFloat(args[5]);

    Camera camera = new Camera(focalLength, new Vector3(cameraX, cameraY, cameraZ));
    WorldObject object = new WorldObject(objFilePath);
    Vector3 rotation = new Vector3(0, OBJ_ROTATE, OBJ_ROTATE);
    List<List<? extends Shape>> preRenderedFrames = new ArrayList<>();

    for (int i = 0; i < numFrames; i++) {
      preRenderedFrames.add(new ArrayList<>());
    }

    // pre-renders the WorldObject in parallel
    IntStream.range(0, numFrames).parallel().forEach((frameNum) -> {
      WorldObject clone = object.clone();
      clone.rotate(rotation.scale(frameNum));
      preRenderedFrames.set(frameNum, Shapes.sortLines(camera.draw(clone)));
    });

    AudioPlayer player = new AudioPlayer(SAMPLE_RATE, preRenderedFrames);
    player.setRotateSpeed(ROTATE_SPEED);
    player.setTranslation(TRANSLATION_SPEED, TRANSLATION);
    player.setScale(SCALE);
    player.setWeight(WEIGHT);

    player.start();
  }
}