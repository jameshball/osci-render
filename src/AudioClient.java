import com.xtaudio.xt.*;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    int sampleRate = 192000;

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);

    // Draws a square.
    Line l1 = new Line(-0.5f, -0.5f, 0.5f, -0.5f);
    Line l2 = new Line(0.5f, -0.5f, 0.5f, 0.5f);
    Line l3 = new Line(0.5f, 0.5f, -0.5f, 0.5f);
    Line l4 = new Line(-0.5f, 0.5f, -0.5f, -0.5f);

    AudioPlayer.addLine(l1);
    AudioPlayer.addLine(l2);
    AudioPlayer.addLine(l3);
    AudioPlayer.addLine(l4);

    player.start();
    while (true) {

    }
  }
}