import com.xtaudio.xt.*;

public class AudioClient {
  private static final int SAMPLE_RATE = 192000;

  public static void main(String[] args) {
    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(SAMPLE_RATE, XtSample.FLOAT32), 0, 0, 2, 0);

    AudioPlayer.addLines(Shapes.generatePolygon(100, 0.5));
    AudioPlayer.addLines(Shapes.generatePolygram(5, 3, 0.5));

    AudioPlayer.setRotateSpeed(0.4);

    player.start();
  }
}