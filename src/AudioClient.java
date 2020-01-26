import com.xtaudio.xt.*;

public class AudioClient {
  private static double phase;

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

    long startTime = System.currentTimeMillis();

    player.start();
    while (true) {
      if (System.currentTimeMillis() - startTime > 1) {
        float diff = nextSine(sampleRate, 100) / 1000f;

        l1.setX1(l1.getX1() - diff);
        l1.setX2(l1.getX2() + diff);
        l2.setX1(l2.getX1() + diff);
        l2.setX2(l2.getX2() + diff);
        l3.setX1(l3.getX1() + diff);
        l3.setX2(l3.getX2() - diff);
        l4.setX1(l4.getX1() - diff);
        l4.setX2(l4.getX2() - diff);

        startTime = System.currentTimeMillis();
      }
    }
  }

  static float nextSine(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.sin(phase * Math.PI);
  }
}