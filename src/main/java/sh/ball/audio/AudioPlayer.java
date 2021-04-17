package sh.ball.audio;

import sh.ball.MovableRenderer;
import xt.audio.Enums.XtSample;
import xt.audio.Enums.XtSetup;
import xt.audio.Enums.XtSystem;
import xt.audio.Structs.XtBuffer;
import xt.audio.Structs.XtBufferSize;
import xt.audio.Structs.XtChannels;
import xt.audio.Structs.XtDeviceStreamParams;
import xt.audio.Structs.XtFormat;
import xt.audio.Structs.XtMix;
import xt.audio.Structs.XtStreamParams;
import xt.audio.XtAudio;
import xt.audio.XtDevice;
import xt.audio.XtPlatform;
import xt.audio.XtSafeBuffer;
import xt.audio.XtService;
import xt.audio.XtStream;

import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.List;

public class AudioPlayer implements MovableRenderer<List<Shape>, Vector2> {

  private static final int SAMPLE_RATE = 192000;
  private static final int BUFFER_SIZE = 20;

  private final XtMix MIX = new XtMix(SAMPLE_RATE, XtSample.FLOAT32);
  private final XtChannels CHANNELS = new XtChannels(0, 0, 2, 0);
  private final XtFormat FORMAT = new XtFormat(MIX, CHANNELS);
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);

  private List<Shape> frame;
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

  public AudioPlayer() {
  }

  public AudioPlayer(double rotateSpeed, double translateSpeed, Vector2 translateVector, double scale, double weight) {
    setRotationSpeed(rotateSpeed);
    setTranslationSpeed(translateSpeed);
    setTranslation(translateVector);
    setScale(scale);
    setQuality(weight);
  }

  private int render(XtStream stream, XtBuffer buffer, Object user) throws InterruptedException {
    XtSafeBuffer safe = XtSafeBuffer.get(stream);
    safe.lock(buffer);
    float[] output = (float[]) safe.getOutput();

    for (int f = 0; f < buffer.frames; f++) {
      Shape shape = getCurrentShape();

      shape = shape.setWeight(weight);
      shape = scale(shape);
      shape = rotate(shape, FORMAT.mix.rate);
      shape = translate(shape, FORMAT.mix.rate);

      double totalAudioFrames = shape.getWeight() * shape.getLength();
      double drawingProgress = totalAudioFrames == 0 ? 1 : audioFramesDrawn / totalAudioFrames;
      Vector2 nextVector = shape.nextVector(drawingProgress);

      output[f * FORMAT.channels.outputs] = (float) nextVector.getX();
      output[f * FORMAT.channels.outputs + 1] = (float) nextVector.getY();

      audioFramesDrawn++;

      if (audioFramesDrawn > totalAudioFrames) {
        audioFramesDrawn = 0;
        currentShape++;
      }

      if (currentShape >= frame.size()) {
        currentShape = 0;
        frame = frameQueue.take();
      }
    }
    safe.unlock(buffer);
    return 0;
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

  @Override
  public void setRotationSpeed(double speed) {
    this.rotateSpeed = speed;
  }

  @Override
  public void setTranslation(Vector2 translation) {
    this.translateVector = translation;
  }

  @Override
  public void setTranslationSpeed(double speed) {
    translateSpeed = speed;
  }

  @Override
  public void setScale(double scale) {
    this.scale = scale;
  }

  @Override
  public void setQuality(double quality) {
    this.weight = quality;
  }

  private Shape getCurrentShape() {
    if (frame.size() == 0) {
      return new Vector2();
    }

    return frame.get(currentShape);
  }

  @Override
  public void run() {
    try {
      frame = frameQueue.take();
    } catch (InterruptedException e) {
      throw new RuntimeException("Initial frame not found. Cannot continue.");
    }

    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtSystem system = platform.setupToSystem(XtSetup.CONSUMER_AUDIO);
      XtService service = platform.getService(system);
      if (service == null) return;

      String defaultOutput = service.getDefaultDeviceId(true);
      if (defaultOutput == null) return;

      try (XtDevice device = service.openDevice(defaultOutput)) {
        if (device.supportsFormat(FORMAT)) {

          XtBufferSize size = device.getBufferSize(FORMAT);
          XtStreamParams streamParams = new XtStreamParams(true, this::render, null, null);
          XtDeviceStreamParams deviceParams = new XtDeviceStreamParams(streamParams, FORMAT, size.current);
          try (XtStream stream = device.openStream(deviceParams, null);
               XtSafeBuffer safe = XtSafeBuffer.register(stream, true)) {
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

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public void addFrame(List<Shape> frame) {
    try {
      frameQueue.put(frame);
    } catch (InterruptedException e) {
      e.printStackTrace();
      System.err.println("Frame missed.");
    }
  }

  private static final class Phase {

    private double value = 0;
  }
}

