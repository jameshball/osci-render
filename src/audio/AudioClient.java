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
  public static final int SAMPLE_RATE = 192000;
  public static final double TARGET_FRAMERATE = 30;

  public static void main(String[] args) {
    // TODO: Calculate weight of lines using depth.
    //  Reduce weight of lines drawn multiple times.
    //  Find intersections of lines to (possibly) improve line cleanup.
    //  Improve performance of line cleanup with a heuristic.

    Camera camera = new Camera(0.6, new Vector3(0, 0, -0.08));
    WorldObject object = new WorldObject(args[0], new Vector3(0, 0, 0), new Vector3());
    Vector3 rotation = new Vector3(0,Math.PI / 100,Math.PI / 100);
    List<List<? extends Shape>> preRenderedFrames = new ArrayList<>();

    int numFrames = (int) (Float.parseFloat(args[1]) * TARGET_FRAMERATE);

    long start = System.currentTimeMillis();

    for (int i = 0; i < numFrames; i++) {
      preRenderedFrames.add(new ArrayList<>());
    }

    IntStream.range(0, numFrames).parallel().forEach((frameNum) -> {
      WorldObject clone = object.clone();
      clone.rotate(rotation.scale(frameNum));
      preRenderedFrames.set(frameNum, Shapes.sortLines(camera.draw(clone)));
    });

    System.out.println(System.currentTimeMillis() - start);

    AudioPlayer player = new AudioPlayer(SAMPLE_RATE, preRenderedFrames);
    //AudioPlayer.setRotateSpeed(1);
    //AudioPlayer.setTranslation(1, new Vector2(1, 1));
    player.start();
  }
}