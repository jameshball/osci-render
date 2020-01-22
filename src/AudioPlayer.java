import com.xtaudio.xt.*;

public class AudioPlayer extends Thread {
  private static double phase;
  public static XtFormat FORMAT;
  private static float count = 1;
  private static volatile boolean stopped = false;

  private static float x1;
  private static float y1;
  private static float x2;
  private static float y2;

  private static float FREQUENCY = 440f;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    count++;

    for (int f = 0; f < frames; f++) {
      for (int c = 0; c < format.outputs; c++) {
        float xMid = (x1 + x2) / 2f;
        float yMid = (y1 + y2) / 2f;

        ((float[]) output)[f * format.outputs] = xMid + (Math.abs(x2 - x1) / 2f) * nextSine(FREQUENCY);
        ((float[]) output)[f * format.outputs + 1] = yMid + (Math.abs(y2 - y1) / 2f) * nextSine(FREQUENCY);
      }
    }
  }

  public static void drawLine(float x1, float y1, float x2, float y2) {
    AudioPlayer.x1 = x1;
    AudioPlayer.y1 = y1;
    AudioPlayer.x2 = x2;
    AudioPlayer.y2 = y2;
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
