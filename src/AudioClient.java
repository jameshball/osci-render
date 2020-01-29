import com.xtaudio.xt.*;

import java.util.ArrayList;
import java.util.List;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    int sampleRate = 192000;

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);

    // Draws a square.
    Line l1 = new Line(-0.5, -0.5, 0.5, -0.5);
    Line l2 = new Line(0.5, -0.5, 0.5, 0.5);
    Line l3 = new Line(0.5, 0.5, -0.5, 0.5);
    Line l4 = new Line(-0.5, 0.5, -0.5, -0.5);

    AudioPlayer.addLine(l1);
    AudioPlayer.addLine(l2);
    AudioPlayer.addLine(l3);
    AudioPlayer.addLine(l4);
    
    AudioPlayer.setRotateSpeed(0.4);

    player.start();
    while (true) {

    }
  }

  public static List<Line> generatePolygon(int sides, double scale) {
    List<Line> polygon = new ArrayList<>();

    Point start = new Point(scale, scale);

    return null;
  }
}