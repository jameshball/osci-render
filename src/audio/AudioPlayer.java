package audio;

import com.xtaudio.xt.*;
import shapes.Shape;
import shapes.Shapes;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class AudioPlayer extends Thread {
  private static double[] phases = new double[2];
  private static XtFormat FORMAT;

  private static List<Shape> shapes = new ArrayList<>();
  private static List<List<? extends Shape>> frames = new ArrayList<>();
  private static int currentFrame = 0;
  private static int currentShape = 0;
  private static long timeOfLastFrame;
  private static int audioFramesDrawn = 0;

  private static double TRANSLATE_SPEED = 0;
  private static Vector2 TRANSLATE_VECTOR;
  private static final int TRANSLATE_PHASE_INDEX = 0;
  private static double ROTATE_SPEED = 0;
  private static final int ROTATE_PHASE_INDEX = 1;
  private static double SCALE = 1;
  private static double WEIGHT = 100;

  private volatile boolean stopped;

  public AudioPlayer(int sampleRate, List<List<? extends Shape>> frames) {
    AudioPlayer.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);
    AudioPlayer.frames = frames;
    AudioPlayer.timeOfLastFrame = System.currentTimeMillis();
  }

  static void render(XtStream stream, Object input, Object output, int audioFrames,
                     double time, long position, boolean timeValid, long error, Object user) {
    for (int f = 0; f < audioFrames; f++) {
      Shape shape = getCurrentShape();

      shape = shape.setWeight(WEIGHT);
      shape = scale(shape);
      shape = rotate(shape, FORMAT.mix.rate);
      shape = translate(shape, FORMAT.mix.rate);

      double totalAudioFrames = shape.getWeight() * shape.getLength();
      double drawingProgress = totalAudioFrames == 0 ? 1 : audioFramesDrawn / totalAudioFrames;

      for (int c = 0; c < FORMAT.outputs; c++) {
        ((float[]) output)[f * FORMAT.outputs] = shape.nextX(drawingProgress);
        ((float[]) output)[f * FORMAT.outputs + 1] = shape.nextY(drawingProgress);
      }

      audioFramesDrawn++;

      if (audioFramesDrawn > totalAudioFrames) {
        audioFramesDrawn = 0;
        currentShape = ++currentShape % AudioPlayer.frames.get(currentFrame).size();
      }

      if (System.currentTimeMillis() - timeOfLastFrame > (float) 1000 / AudioClient.TARGET_FRAMERATE) {
        currentShape = 0;
        currentFrame = ++currentFrame % AudioPlayer.frames.size();
        timeOfLastFrame = System.currentTimeMillis();
      }
    }
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

  public static void addShapes(List<? extends Shape> newShapes) {
    shapes.addAll(newShapes);
  }

  public static void updateFrame(List<? extends Shape> frame) {
    currentShape = 0;
    shapes = new ArrayList<>();
    shapes.addAll(frame);
    // Arbitrary function for changing weights based on frame draw-time.
    AudioPlayer.WEIGHT = 200 * Math.exp(-0.017 * Shapes.totalLength(frame));
  }

  private static Shape getCurrentShape() {
    if (frames.size() == 0 || frames.get(currentFrame).size() == 0) {
      return new Vector2(0, 0);
    }

    return frames.get(currentFrame).get(currentShape);
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