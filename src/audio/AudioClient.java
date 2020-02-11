package audio;

import engine.Camera;
import engine.Vector3;
import engine.WorldObject;
import shapes.Ellipse;
import shapes.Shapes;
import shapes.Vector2;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) throws InterruptedException {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

    Camera camera = new Camera(0.6, new Vector3(0, 0, -2), 60);
    WorldObject cube = new WorldObject(args[0], new Vector3(0,0,0), new Vector3());

    player.start();

    AudioPlayer.setScale(0.5);

    while(true) {
      AudioPlayer.updateFrame(camera.draw(cube));
      cube.rotate(new Vector3(
        0,
        Math.PI / 100,
        0
      ));
      Thread.sleep(1000/30);
    }
  }
}