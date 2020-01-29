import com.xtaudio.xt.*;

import java.util.ArrayList;
import java.util.List;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    int sampleRate = 192000;

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);

    for (int i = 2; i < 7; i++) {
      AudioPlayer.addLines(generatePolygon(i, 0.5));
    }
    AudioPlayer.setRotateSpeed(0.4);

    player.start();
    while (true) {

    }
  }

  public static List<Line> generatePolygon(int sides, double scale) {
    List<Line> polygon = new ArrayList<>();

    double theta = 2 * Math.PI / sides;

    Point start = new Point(scale, scale);
    Point rotated = start.rotate(theta);

    polygon.add(new Line(start, rotated));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta)));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }
}