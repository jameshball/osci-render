package audio;

import com.xtaudio.xt.*;
import shapes.Line;
import shapes.Vector;

import java.util.ArrayList;
import java.util.List;

public class AudioPlayer extends Thread {
  public static XtFormat FORMAT;
  private static volatile boolean stopped = false;
  private static List<Line> lines = new ArrayList<>();
  private static int currentLine = 0;
  private static int framesDrawn = 0;

  private static double[] phases = new double[2];

  private static double TRANSLATE_SPEED = 0;
  private static Vector TRANSLATE_VECTOR;
  private static final int TRANSLATE_PHASE_INDEX = 0;
  private static double ROTATE_SPEED = 0.4;
  private static final int ROTATE_PHASE_INDEX = 1;
  private static double SCALE;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    for (int f = 0; f < frames; f++) {
      Line line = currentLine();

      line = scale(line);
      line = rotate(line, stream.getFormat().mix.rate);
      line = translate(line, stream.getFormat().mix.rate);

      int framesToDraw = (int) (line.length * line.getWeight());

      for (int c = 0; c < format.outputs; c++) {
        ((float[]) output)[f * format.outputs] = nextX(line, framesToDraw);
        ((float[]) output)[f * format.outputs + 1] = nextY(line, framesToDraw);
      }

      framesDrawn++;

      if (framesDrawn > framesToDraw) {
        framesDrawn = 0;
        currentLine++;
      }
    }
  }

  static float nextTheta(double sampleRate, double frequency, int phaseIndex) {
    phases[phaseIndex] += frequency / sampleRate;
    if (phases[phaseIndex] >= 1.0)
      phases[phaseIndex] = -1.0;
    return (float) (phases[phaseIndex] * Math.PI);
  }

  private static float nextX(Line line, double framesToDraw) {
    return (float) (line.getX1() + (line.getX2() - line.getX1()) * framesDrawn / framesToDraw);
  }

  private static float nextY(Line line, double framesToDraw) {
    return (float) (line.getY1() + (line.getY2() - line.getY1()) * framesDrawn / framesToDraw);
  }

  private static Line scale(Line line) {
    if (SCALE != 1) {
      return line.scale(SCALE);
    }

    return line;
  }

  private static Line rotate(Line line, double sampleRate) {
    if (ROTATE_SPEED != 0) {
      line = line.rotate(
        nextTheta(sampleRate, ROTATE_SPEED, TRANSLATE_PHASE_INDEX)
      );
    }

    return line;
  }

  private static Line translate(Line line, double sampleRate) {
    if (TRANSLATE_SPEED != 0 && !TRANSLATE_VECTOR.equals(new Vector())) {
      return line.translate(TRANSLATE_VECTOR.scale(
        Math.sin(nextTheta(sampleRate, TRANSLATE_SPEED, ROTATE_PHASE_INDEX))
      ));
    }

    return line;
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

  public static void setTranslation(double speed, Vector translation) {
    AudioPlayer.TRANSLATE_SPEED = speed;
    AudioPlayer.TRANSLATE_VECTOR = translation;
  }

  public static void setScale(double scale) {
    AudioPlayer.SCALE = scale;
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

  public static XtFormat defaultFormat(int sampleRate) {
    return new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);
  }
}
