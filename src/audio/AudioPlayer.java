package audio;

import com.xtaudio.xt.*;
import shapes.Shape;
import shapes.Shapes;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

public class AudioPlayer extends Thread {
  private static double[] phases = new double[2];
  private static XtFormat FORMAT;

  private static List<Shape> shapes = new ArrayList<>();
  private static Lock lock = new ReentrantLock();
  private static int currentShape = 0;
  private static int framesDrawn = 0;

  private static double TRANSLATE_SPEED = 0;
  private static Vector2 TRANSLATE_VECTOR;
  private static final int TRANSLATE_PHASE_INDEX = 0;
  private static double ROTATE_SPEED = 0;
  private static final int ROTATE_PHASE_INDEX = 1;
  private static double SCALE = 1;
  private static double WEIGHT = 100;

  private boolean stopped;

  public AudioPlayer(int sampleRate, double frequency) {
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);
  }

  static void render(XtStream stream, Object input, Object output, int frames,
                     double time, long position, boolean timeValid, long error, Object user) {
    lock.lock();
    for (int f = 0; f < frames; f++) {
      Shape shape = getCurrentShape();

      shape = shape.setWeight(WEIGHT);
      shape = scale(shape);
      shape = rotate(shape, FORMAT.mix.rate);
      shape = translate(shape, FORMAT.mix.rate);

      double framesToDraw = shape.getWeight() * shape.getLength();
      double drawingProgress = framesToDraw == 0 ? 1 : framesDrawn / framesToDraw;

      for (int c = 0; c < FORMAT.outputs; c++) {
        ((float[]) output)[f * FORMAT.outputs] = (float) shape.nextX(drawingProgress);
        ((float[]) output)[f * FORMAT.outputs + 1] = (float) shape.nextY(drawingProgress);
      }

      framesDrawn++;

      if (framesDrawn > framesToDraw) {
        framesDrawn = 0;
        currentShape++;
      }
    }
    lock.unlock();
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
    if (TRANSLATE_SPEED != 0 && !TRANSLATE_VECTOR.equals(new Vector2())) {
      return shape.translate(TRANSLATE_VECTOR.scale(
        Math.sin(nextTheta(sampleRate, TRANSLATE_SPEED, ROTATE_PHASE_INDEX))
      ));
    }

    return shape;
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

  public static void setRotateSpeed(double speed) {
    AudioPlayer.ROTATE_SPEED = speed;
  }

  public static void setTranslation(double speed, Vector2 translation) {
    AudioPlayer.TRANSLATE_SPEED = speed;
    AudioPlayer.TRANSLATE_VECTOR = translation;
  }

  public static void setScale(double scale) {
    AudioPlayer.SCALE = scale;
  }

  public static void addShape(Shape shape) {
    shapes.add(shape);
  }

  public static void addShapes(List<Shape> newShapes) {
    shapes.addAll(newShapes);
  }

  public static void updateFrame(List<Shape> frame) {
    lock.lock();
    currentShape = 0;
    shapes = new ArrayList<>();
    shapes.addAll(frame);
    AudioPlayer.WEIGHT = 200 * Math.exp(-0.017 * Shapes.totalLength(frame));
    lock.unlock();
  }

  private static Shape getCurrentShape() {
    if (shapes.size() == 0) {
      return new Vector2(0, 0);
    }

    if (currentShape >= shapes.size()) {
      currentShape -= shapes.size();
    }

    return shapes.get(currentShape);
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