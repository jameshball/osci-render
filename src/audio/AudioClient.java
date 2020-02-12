package audio;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import shapes.Shapes;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;
  private static final double FRAMERATE = 30;

  public static void main(String[] args) throws InterruptedException {
    AudioPlayer player = new AudioPlayer(SAMPLE_RATE, 440);

    Camera camera = new Camera(0.6, new Vector3(0, 0, -3));
    WorldObject cube = new WorldObject(args[0], new Vector3(0, 0, 0), new Vector3());
    Vector3 rotation = new Vector3(0,Math.PI / 100,Math.PI / 100);

    player.start();

    while (true) {
      AudioPlayer.updateFrame(Shapes.sortLines(camera.draw(cube)));
      cube.rotate(rotation);
      Thread.sleep((long) (1000 / FRAMERATE));
    }
  }
}