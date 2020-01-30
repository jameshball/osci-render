package audio;

import shapes.Shapes;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

    AudioPlayer.addLines(Shapes.generatePolygon(100, 0.5, 50));
    AudioPlayer.addLines(Shapes.generatePolygram(5, 3, 0.5, 50));

    AudioPlayer.setRotateSpeed(0.4);

    player.start();
  }
}