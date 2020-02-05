package audio;

import shapes.Ellipse;
import shapes.Shapes;
import shapes.Vector;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

    AudioPlayer.addShapes(Shapes.generatePolygram(5, 3, 0.5, 60));
    AudioPlayer.addShape(new Ellipse(0.7, 0.7));

    AudioPlayer.setRotateSpeed(0.8);
    AudioPlayer.setTranslation(2, new Vector(0.5, 0.5));
    AudioPlayer.setScale(0.5);

    player.start();
  }
}