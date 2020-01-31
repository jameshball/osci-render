package audio;

import shapes.Shapes;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

    for (int i = 2; i < 8; i++) {
      AudioPlayer.addLines(Shapes.generatePolygon(i, 0.5, 100));
    }

//    AudioPlayer.addLines(Shapes.generatePolygon(100, 0.5, 60));
//    AudioPlayer.addLines(Shapes.generatePolygram(5, 3, 0.5, 60));

    AudioPlayer.setRotateSpeed(0.4);

    player.start();
  }
}