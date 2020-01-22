import com.xtaudio.xt.*;

public class AudioPlayer extends Thread {
  private static double phase;
  public static XtFormat FORMAT;
  private static float count = 1;
  private static volatile boolean stopped = false;

  public static float x1;
  public static float y1;
  public static float x2;
  public static float y2;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    count++;

    for (int f = 0; f < frames; f++) {
      float sine = nextSine(format.mix.rate, 440);
      float cos = nextCos(format.mix.rate, 440);

      for (int c = 0; c < format.outputs; c++) {
        float xMid = (x1 + x2) / 2f;
        float yMid = (y1 + y2) / 2f;

        ((float[]) output)[f * format.outputs] = xMid + (Math.abs(x2 - x1) / 2f) * sine;
        ((float[]) output)[f * format.outputs + 1] = yMid + (Math.abs(y2 - y1) / 2f) * sine;
      }
    }
  }

  public static void drawLine(float x1, float y1, float x2, float y2) {
    AudioPlayer.x1 = x1;
    AudioPlayer.y1 = y1;
    AudioPlayer.x2 = x2;
    AudioPlayer.y2 = y2;
  }

  private static float nextSine(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.sin(phase * Math.PI);
  }

  private static float nextCos(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
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
