import com.xtaudio.xt.*;

public class AudioClient {
  public static void main(String[] args) throws Exception {
    AudioPlayer player = new AudioPlayer();
    AudioPlayer.FORMAT = new XtFormat(new XtMix(192000, XtSample.FLOAT32), 0, 0, 2, 0);

    long startTime = System.nanoTime();

    player.start();
    while (true) {
      float currentTime = System.nanoTime() - startTime;
      AudioPlayer.drawLine(0, 0, 0.5f * (float) Math.sin(2 * Math.PI * (currentTime / 100000f)), 0.5f * (float) Math.cos(2 * Math.PI * (currentTime / 100000f)));
    }
  }
}