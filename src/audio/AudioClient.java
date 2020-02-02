package audio;

import shapes.Shapes;
import shapes.Vector;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

    AudioPlayer.addLines(Shapes.generatePolygon(100, 0.5, 60));
    AudioPlayer.addLines(Shapes.generatePolygram(5, 3, 0.5, 60));

    AudioPlayer.setRotateSpeed(0.8);
    AudioPlayer.setTranslation(4, new Vector(1, 1));
    AudioPlayer.setScale(0.5);

    player.start();
  }
}