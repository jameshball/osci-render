import com.xtaudio.xt.*;

import java.util.ArrayList;
import java.util.List;

public class AudioPlayer extends Thread {
  public static XtFormat FORMAT;
  private static volatile boolean stopped = false;
  private static List<Line> lines = new ArrayList<>();
  private static int currentLine = 0;
  private static float FREQUENCY = 440;
  private static double ROTATE_SPEED = 0;
  private static int framesDrawn = 0;
  private static double phase = 0;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    for (int f = 0; f < frames; f++) {
      Line line;

      if (ROTATE_SPEED == 0) {
        line = currentLine();
      } else {
        line = currentLine().rotate(nextTheta(stream.getFormat().mix.rate, ROTATE_SPEED));
      }

      int framesToDraw = (int) (line.length() * 100);

      for (int c = 0; c < format.outputs; c++) {
        ((float[]) output)[f * format.outputs] = (float) (line.getX1() + (line.getX2() - line.getX1()) * framesDrawn / framesToDraw);
        ((float[]) output)[f * format.outputs + 1] = (float) (line.getY1() + (line.getY2() - line.getY1()) * framesDrawn / framesToDraw);
      }

      framesDrawn++;

      if (framesDrawn > framesToDraw) {
        framesDrawn = 0;
        currentLine++;
      }
    }
  }

  static float nextTheta(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) (phase * Math.PI);
  }

  public static void addLine(Line line) {
    AudioPlayer.lines.add(line);
  }

  public static void addLines(List<Line> lines) {
    AudioPlayer.lines.addAll(lines);
  }

  private static Line currentLine() {
    return lines.get(currentLine % lines.size());
  }

  public static void setRotateSpeed(double speed) {
    AudioPlayer.ROTATE_SPEED = speed;
  }

  @Override
  public void run() {
    try (XtAudio audio = new XtAudio(null, null, null, null)) {
      XtService service = XtAudio.getServiceBySetup(XtSetup.CONSUMER_AUDIO);

      try (XtDevice device = service.openDefaultDevice(true)) {
        if (device != null && device.supportsFormat(FORMAT)) {

          XtBuffer buffer = device.getBuffer(FORMAT);
          try (XtStream stream = device.openStream(FORMAT, true, false,
            buffer.current, AudioPlayer::render, null, null)) {
            stream.start();
            while (!stopped) {
              Thread.onSpinWait();
            }
            stream.stop();
          }
        }
      }
    }
  }

  public void stopPlaying() {
    stopped = true;
  }
}
