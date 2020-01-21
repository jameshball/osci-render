import com.xtaudio.xt.*;

public class AudioClient {

  static double phase = 0.0;
  static final XtFormat FORMAT = new XtFormat(new XtMix(192000, XtSample.FLOAT32), 0, 0, 2, 0);
  static float count = 1;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {

    XtFormat format = stream.getFormat();

    count++;

    for (int f = 0; f < frames; f++) {
      float sine = nextSine(format.mix.rate, count * 0.05);
      float cos = nextCos(format.mix.rate, count * 0.05);

      for (int c = 0; c < format.outputs; c++) {
        float x1 = -Math.min(0.01f * count, 1);;
        float y1 = 0;
        float x2 = Math.min(0.01f * count, 1);
        float y2 = 0.5f;

        float xMid = (x1 + x2) / 2f;
        float yMid = (y1 + y2) / 2f;

        ((float[]) output)[f * format.outputs] = xMid + (Math.abs(x2 - x1) / 2f) * sine;
        ((float[]) output)[f * format.outputs + 1] = yMid + (Math.abs(y2 - y1) / 2f) * sine;
      }
    }
  }

  static float nextSine(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.sin(phase * Math.PI);
  }

  static float nextCos(double sampleRate, double frequency) {
    phase += frequency / sampleRate;
    if (phase >= 1.0)
      phase = -1.0;
    return (float) Math.cos(phase * Math.PI);
  }

  public static void main(String[] args) throws Exception {

    try (XtAudio audio = new XtAudio(null, null, null, null)) {
      XtService service = XtAudio.getServiceBySetup(XtSetup.CONSUMER_AUDIO);
      try (XtDevice device = service.openDefaultDevice(true)) {
        if (device != null && device.supportsFormat(FORMAT)) {

          XtBuffer buffer = device.getBuffer(FORMAT);
          try (XtStream stream = device.openStream(FORMAT, true, false,
            buffer.current, AudioClient::render, null, null)) {
            stream.start();
            Thread.sleep(10000);
            stream.stop();
          }
        }
      }
    }
  }
}