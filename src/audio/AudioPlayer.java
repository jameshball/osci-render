package audio;

import com.xtaudio.xt.*;
import shapes.Ellipse;
import shapes.Line;
import shapes.Shape;
import shapes.Vector;

import java.util.ArrayList;
import java.util.List;

public class AudioPlayer extends Thread {
  private static volatile boolean stopped = false;
  private static List<Shape> shapes = new ArrayList<>();
  private static int currentShape = 0;
  private static int framesDrawn = 0;

  private static double[] phases = new double[2];

  public static XtFormat FORMAT;
  private static double TRANSLATE_SPEED = 0;
  private static Vector TRANSLATE_VECTOR;
  private static final int TRANSLATE_PHASE_INDEX = 0;
  private static double ROTATE_SPEED = 0.4;
  private static final int ROTATE_PHASE_INDEX = 1;
  private static double SCALE = 1;

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    XtFormat format = stream.getFormat();

    for (int f = 0; f < frames; f++) {
      Shape shape = currentShape();

      shape = scale(shape);
      shape = rotate(shape, stream.getFormat().mix.rate);
      shape = translate(shape, stream.getFormat().mix.rate);

      double framesToDraw = shape.getLength() * shape.getWeight();
      double drawingProgress = framesDrawn / framesToDraw;

      for (int c = 0; c < format.outputs; c++) {
        ((float[]) output)[f * format.outputs] = shape.nextX(drawingProgress);
        ((float[]) output)[f * format.outputs + 1] = shape.nextY(drawingProgress);
      }

      framesDrawn++;

      if (framesDrawn > framesToDraw) {
        framesDrawn = 0;
        currentShape++;
      }
    }
  }

  static double nextTheta(double sampleRate, double frequency, int phaseIndex) {
    phases[phaseIndex] += frequency / sampleRate;

    if (phases[phaseIndex] >= 1.0) {
      phases[phaseIndex] = -1.0;
    }

    return phases[phaseIndex] * Math.PI;
  }

  private static Shape scale(Shape shape) {
    if (SCALE != 1) {
      return shape.scale(SCALE);
    }

    return shape;
  }

  private static Shape rotate(Shape shape, double sampleRate) {
    if (ROTATE_SPEED != 0) {
      shape = shape.rotate(
        nextTheta(sampleRate, ROTATE_SPEED, TRANSLATE_PHASE_INDEX)
      );
    }

    return shape;
  }

  private static Shape translate(Shape shape, double sampleRate) {
    if (TRANSLATE_SPEED != 0 && !TRANSLATE_VECTOR.equals(new Vector())) {
      return shape.translate(TRANSLATE_VECTOR.scale(
        Math.sin(nextTheta(sampleRate, TRANSLATE_SPEED, ROTATE_PHASE_INDEX))
      ));
    }

    return shape;
  }

  public static void addShape(Shape shape) {
    AudioPlayer.shapes.add(shape);
  }

  public static void addShapes(List<Shape> shapes) {
    AudioPlayer.shapes.addAll(shapes);
  }

  private static Shape currentShape() {
    return shapes.get(currentShape % shapes.size());
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
