package audio;

import com.xtaudio.xt.XtAudio;
import com.xtaudio.xt.XtBuffer;
import com.xtaudio.xt.XtDevice;
import com.xtaudio.xt.XtFormat;
import com.xtaudio.xt.XtMix;
import com.xtaudio.xt.XtSample;
import com.xtaudio.xt.XtService;
import com.xtaudio.xt.XtSetup;
import com.xtaudio.xt.XtStream;
import shapes.Shape;
import shapes.Vector2;

import java.util.List;

public class AudioPlayer implements Runnable {

  private final XtFormat FORMAT;

  private final List<List<Shape>> frames;
  private int currentFrame = 0;
  private int currentShape = 0;
  private int audioFramesDrawn = 0;

  private double translateSpeed = 0;
  private Vector2 translateVector = new Vector2();
  private final Phase translatePhase = new Phase();
  private double rotateSpeed = 0;
  private final Phase rotatePhase = new Phase();
  private double scale = 1;
  private double weight = Shape.DEFAULT_WEIGHT;

  private volatile boolean stopped;

  public AudioPlayer(int sampleRate, List<List<Shape>> frames) {
    this.FORMAT = new XtFormat(new XtMix(sampleRate, XtSample.FLOAT32), 0, 0, 2, 0);
    this.frames = frames;
  }

  public AudioPlayer(int sampleRate, List<List<Shape>> frames, float rotateSpeed,
      float translateSpeed, Vector2 translateVector, float scale, float weight) {
    this(sampleRate, frames);
    setRotateSpeed(rotateSpeed);
    setTranslation(translateSpeed, translateVector);
    setScale(scale);
    setWeight(weight);
  }

  private void render(XtStream stream, Object input, Object output, int audioFrames,
      double time, long position, boolean timeValid, long error, Object user) {
    for (int f = 0; f < audioFrames; f++) {
      Shape shape = getCurrentShape();

      shape = shape.setWeight(weight);
      shape = scale(shape);
      shape = rotate(shape, FORMAT.mix.rate);
      shape = translate(shape, FORMAT.mix.rate);

      double totalAudioFrames = shape.getWeight() * shape.getLength();
      double drawingProgress = totalAudioFrames == 0 ? 1 : audioFramesDrawn / totalAudioFrames;
      Vector2 nextVector = shape.nextVector(drawingProgress);

      ((float[]) output)[f * FORMAT.outputs] = (float) nextVector.getX();
      ((float[]) output)[f * FORMAT.outputs + 1] = (float) nextVector.getY();

      audioFramesDrawn++;

      if (audioFramesDrawn > totalAudioFrames) {
        audioFramesDrawn = 0;
        currentShape++;
      }

      if (currentShape >= frames.get(currentFrame).size()) {
        currentShape = 0;
        currentFrame = ++currentFrame % frames.size();
      }
    }
  }

  private Shape rotate(Shape shape, double sampleRate) {
    if (rotateSpeed != 0) {
      shape = shape.rotate(
          nextTheta(sampleRate, rotateSpeed, translatePhase)
      );
    }

    return shape;
  }

  private Shape translate(Shape shape, double sampleRate) {
    if (translateSpeed != 0 && !translateVector.equals(new Vector2())) {
      return shape.translate(translateVector.scale(
          Math.sin(nextTheta(sampleRate, translateSpeed, rotatePhase))
      ));
    }

    return shape;
  }

  private double nextTheta(double sampleRate, double frequency, Phase phase) {
    phase.value += frequency / sampleRate;

    if (phase.value >= 1.0) {
      phase.value = -1.0;
    }

    return phase.value * Math.PI;
  }

  private Shape scale(Shape shape) {
    if (scale != 1) {
      return shape.scale(scale);
    }

    return shape;
  }

  public void setRotateSpeed(double speed) {
    this.rotateSpeed = speed;
  }

  public void setTranslation(double speed, Vector2 translation) {
    this.translateSpeed = speed;
    this.translateVector = translation;
  }

  public void setScale(double scale) {
    this.scale = scale;
  }

  public void setWeight(double weight) {
    this.weight = weight;
  }

  private Shape getCurrentShape() {
    if (frames.size() == 0 || frames.get(currentFrame).size() == 0) {
      return new Vector2(0, 0);
    }

    return frames.get(currentFrame).get(currentShape);
  }

  public void addFrame(List<Shape> frame) {
    frames.add(frame);
  }

  @Override
  public void run() {
    try (XtAudio audio = new XtAudio(null, null, null, null)) {
      XtService service = XtAudio.getServiceBySetup(XtSetup.CONSUMER_AUDIO);
      try (XtDevice device = service.openDefaultDevice(true)) {
        if (device != null && device.supportsFormat(FORMAT)) {
          XtBuffer buffer = device.getBuffer(FORMAT);

          try (XtStream stream = device.openStream(FORMAT, true, false,
              buffer.current, this::render, null, null)) {
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

  private static final class Phase {

    private double value = 0;
  }
}

