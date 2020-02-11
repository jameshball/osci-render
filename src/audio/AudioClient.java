package audio;

import shapes.Ellipse;
import shapes.Shapes;
import shapes.Vector2;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    // TODO: Implement L-Systems drawing and Chinese postman.

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = AudioPlayer.defaultFormat(SAMPLE_RATE);

//    AudioPlayer.addShapes(Shapes.generatePolygram(5, 3, 0.5, 60));
    //AudioPlayer.addShapes(Shapes.generatePolygon(500, 0.5, 10));
//    AudioPlayer.addShape(new Ellipse(0.5, 1));

//    AudioPlayer.setRotateSpeed(0.8);
//    AudioPlayer.setTranslation(2, new Vector(0.5, 0.5));

    AudioPlayer.addShapes(Shapes.generatePolygon(4, new Vector2(0.25, 0.25)));
    AudioPlayer.addShapes(Shapes.generatePolygon(4, new Vector2(0.5, 0.5)));
    AudioPlayer.addShapes(Shapes.generatePolygon(4, new Vector2(-0.8, -0.8)));
    for (int i = 0; i < 10; i++) {
      AudioPlayer.addShape(new Ellipse(i / 10d, i / 10d));
    }

    AudioPlayer.setScale(0.5);

    player.start();
  }
}