import com.xtaudio.xt.*;

import java.util.ArrayList;
import java.util.List;

public class AudioPlayer extends Thread {
  private static double phase;
  public static XtFormat FORMAT;
  private static int count = 0;
  private static volatile boolean stopped = false;
  private static List<Line> lines = new ArrayList<>();

  private static float FREQUENCY = 440f;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    Line line = lines.get(count % lines.size());

    int neg = line.getX1() < line.getX2() || line.getY1() < line.getY2() ? 1 : -1;

    for (int f = 0; f < frames; f++) {
      for (int c = 0; c < format.outputs; c++) {
        ((float[]) output)[f * format.outputs] = line.getX1() + Math.abs(line.getX2() - line.getX1()) * (float) (neg * f) / frames;
        ((float[]) output)[f * format.outputs + 1] = line.getY1() + Math.abs(line.getY2() - line.getY1()) * (float) (neg * f) / frames;
      }
    }

    count++;
  }

  public static void addLine(Line line) {
    AudioPlayer.lines.add(line);
  }

  public static void setFrequency(float frequency) {
    AudioPlayer.FREQUENCY = frequency;
  }

  public static float nextSine(double frequency) {
    phase += frequency / FORMAT.mix.rate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.sin(phase * Math.PI);
  }

  public static float nextCos(double frequency) {
    phase += frequency / FORMAT.mix.rate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.cos(phase * Math.PI);
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
