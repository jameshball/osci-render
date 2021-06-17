package sh.ball.audio;

import sh.ball.audio.effect.Effect;

import java.io.*;
import java.util.*;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.AudioEngine;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import java.util.concurrent.Semaphore;
import java.util.concurrent.locks.ReentrantLock;

public class ShapeAudioPlayer implements AudioPlayer<List<Shape>> {

  // Arbitrary max count for effects
  private static final int MAX_COUNT = 10000;
  private static final int BUFFER_SIZE = 5;
  // Is this always true? Might need to check from AudioEngine
  private static final int BITS_PER_SAMPLE = 16;
  private static final boolean SIGNED = true;
  private static final boolean BIG_ENDIAN = false;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;

  private final AudioEngine audioEngine;
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final Map<Object, Effect> effects = new HashMap<>();
  private final ReentrantLock renderLock = new ReentrantLock();
  private final List<Listener> listeners = new ArrayList<>();

  private ByteArrayOutputStream outputStream;
  private boolean recording = false;
  private int framesRecorded = 0;
  private List<Shape> frame;
  private int currentShape = 0;
  private int audioFramesDrawn = 0;
  private int count = 0;

  private double weight = Shape.DEFAULT_WEIGHT;
  private AudioDevice device;

  public ShapeAudioPlayer(AudioEngine audioEngine) {
    this.audioEngine = audioEngine;
  }

  private Vector2 generateChannels() throws InterruptedException {
    Shape shape = getCurrentShape().setWeight(weight);

    double totalAudioFrames = shape.getWeight() * shape.getLength();
    double drawingProgress = totalAudioFrames == 0 ? 1 : audioFramesDrawn / totalAudioFrames;
    Vector2 nextVector = applyEffects(count, shape.nextVector(drawingProgress));

    Vector2 channels = cutoff(nextVector);
    writeChannels((float) channels.getX(), (float) channels.getY());

    audioFramesDrawn++;

    if (++count > MAX_COUNT) {
      count = 0;
    }

    if (audioFramesDrawn > totalAudioFrames) {
      audioFramesDrawn = 0;
      currentShape++;
    }

    if (currentShape >= frame.size()) {
      currentShape = 0;
      frame = frameQueue.take();
    }

    return channels;
  }

  private void writeChannels(float leftChannel, float rightChannel) {
    int left = (int)(leftChannel * Short.MAX_VALUE);
    int right = (int)(rightChannel * Short.MAX_VALUE);

    byte b0 = (byte) left;
    byte b1 = (byte)(left >> 8);
    byte b2 = (byte) right;
    byte b3 = (byte)(right >> 8);

    if (recording) {
      outputStream.write(b0);
      outputStream.write(b1);
      outputStream.write(b2);
      outputStream.write(b3);
    }

    for (Listener listener : listeners) {
      listener.write(b0);
      listener.write(b1);
      listener.write(b2);
      listener.write(b3);
      listener.notifyIfFull();
    }

    framesRecorded++;
  }

  private Vector2 cutoff(Vector2 vector) {
    if (vector.getX() < -1) {
      vector = vector.setX(-1);
    } else if (vector.getX() > 1) {
      vector = vector.setX(1);
    }
    if (vector.getY() < -1) {
      vector = vector.setY(-1);
    } else if (vector.getY() > 1) {
      vector = vector.setY(1);
    }
    return vector;
  }

  private Vector2 applyEffects(int frame, Vector2 vector) {
    for (Effect effect : effects.values()) {
      vector = effect.apply(frame, vector);
    }
    return vector;
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

    if (device == null) {
      throw new IllegalArgumentException("No AudioDevice provided");
    }

    audioEngine.play(this::generateChannels, renderLock, device);
  }

  @Override
  public void stop() {
    audioEngine.stop();
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

  @Override
  public void addEffect(Object identifier, Effect effect) {
    effects.put(identifier, effect);
  }

  @Override
  public void removeEffect(Object identifier) {
    effects.remove(identifier);
  }

  @Override
  public void read(byte[] buffer) throws InterruptedException {
    Listener listener = new Listener(buffer);
    try {
      renderLock.lock();
      listeners.add(listener);
    } finally {
      renderLock.unlock();
    }
    listener.waitUntilFull();
    try {
      renderLock.lock();
      listeners.remove(listener);
    } finally {
      renderLock.unlock();
    }
  }

  @Override
  public void startRecord() {
    outputStream = new ByteArrayOutputStream();
    framesRecorded = 0;
    recording = true;
  }

  @Override
  public void setDevice(AudioDevice device) {
    this.device = device;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return audioEngine.getDefaultDevice();
  }

  @Override
  public AudioDevice getDevice() {
    return device;
  }

  @Override
  public List<AudioDevice> devices() {
    return audioEngine.devices();
  }

  @Override
  public AudioInputStream stopRecord() {
    recording = false;
    if (outputStream == null) {
      throw new UnsupportedOperationException("Cannot stop recording before first starting to record");
    }
    byte[] input = outputStream.toByteArray();
    outputStream = null;

    AudioFormat audioFormat = new AudioFormat(device.sampleRate(), BITS_PER_SAMPLE, NUM_OUTPUTS, SIGNED, BIG_ENDIAN);

    return new AudioInputStream(new ByteArrayInputStream(input), audioFormat, framesRecorded);
  }

  private static class Listener {
    private final byte[] buffer;
    private final Semaphore sema;

    private int offset;

    private Listener(byte[] buffer) {
      this.buffer = buffer;
      this.sema = new Semaphore(0);
    }

    private void waitUntilFull() throws InterruptedException {
      sema.acquire();
    }

    private void notifyIfFull() {
      if (offset >= buffer.length) {
        sema.release();
      }
    }

    private void write(byte b) {
      if (offset < buffer.length) {
        buffer[offset++] = b;
      }
    }
  }
}
