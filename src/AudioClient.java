import com.xtaudio.xt.*;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(192000, XtSample.FLOAT32), 0, 0, 2, 0);

    long startTime = System.nanoTime();

    // Draws a square.
    AudioPlayer.addLine(new Line(-0.5f, -0.5f, 0.5f, -0.5f));
    AudioPlayer.addLine(new Line(0.5f, -0.5f, 0.5f, 0.5f));
    AudioPlayer.addLine(new Line(0.5f, 0.5f, -0.5f, 0.5f));
    AudioPlayer.addLine(new Line(-0.5f, 0.5f, -0.5f, -0.5f));

    player.start();
    while (true) {

    }
  }
}