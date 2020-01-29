import com.xtaudio.xt.*;

import java.util.ArrayList;
import java.util.List;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    int sampleRate = 192000;

    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);

    for (int i = 2; i < 7; i++) {
      AudioPlayer.addLines(Shapes.generatePolygon(i, 0.5));
    }

    //AudioPlayer.addLines(Shapes.generatePolygram(5, 4, new Point(0, 0.4).rotate(Math.PI / 5)));

    AudioPlayer.setRotateSpeed(0.8);

    player.start();
    while (true) {

    }
  }
}