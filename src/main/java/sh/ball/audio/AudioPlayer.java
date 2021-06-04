package sh.ball.audio;

import sh.ball.audio.effect.Effect;
import xt.audio.*;
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

import java.io.*;
import java.util.*;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;

import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;
import java.util.concurrent.Semaphore;
import java.util.concurrent.locks.ReentrantLock;

public class AudioPlayer implements Renderer<List<Shape>, AudioInputStream> {

  private static final int BUFFER_SIZE = 5;
  private static final int BITS_PER_SAMPLE = 16;
  private static final boolean SIGNED = true;
  private static final boolean BIG_ENDIAN = false;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;

  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final Map<Object, Effect> effects = new HashMap<>();
  private final ReentrantLock lock = new ReentrantLock();
  private final List<Listener> listeners = new ArrayList<>();

  private final int sampleRate;

  private ByteArrayOutputStream outputStream;
  private boolean recording = false;
  private int framesRecorded = 0;
  private List<Shape> frame;
  private int currentShape = 0;
  private int audioFramesDrawn = 0;

  private double weight = Shape.DEFAULT_WEIGHT;

  private volatile boolean stopped;

  public AudioPlayer() {
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtSystem system = platform.setupToSystem(XtSetup.SYSTEM_AUDIO);
      XtService service = getService(platform, system);
      String deviceId = getDeviceId(service);

      try (XtDevice device = service.openDevice(deviceId)) {
        // Gets the current mix of the device. E.g. 192000 hz and float 32.
        XtMix deviceMix = device.getMix().orElseThrow(RuntimeException::new);
        this.sampleRate = deviceMix.rate;
      }
    }
  }

  private XtService getService(XtPlatform platform, XtSystem system) {
    XtService service = platform.getService(system);
    if (service == null) {
      service = platform.getService(platform.setupToSystem(XtSetup.PRO_AUDIO));
    }
    if (service == null) {
      service = platform.getService(platform.setupToSystem(XtSetup.CONSUMER_AUDIO));
    }
    if (service == null) {
      throw new RuntimeException("Failed to connect to any audio service");
    }

    if (service.getCapabilities().contains(Enums.XtServiceCaps.NONE)) {
      throw new RuntimeException("Audio service has no capabilities");
    }

    return service;
  }

  private String getDeviceId(XtService service) {
    String deviceId = service.getDefaultDeviceId(true);
    if (deviceId == null) {
      return  getFirstDevice(service);
    }
    return deviceId;
  }

  private int render(XtStream stream, XtBuffer buffer, Object user) throws InterruptedException {
    XtSafeBuffer safe = XtSafeBuffer.get(stream);
    safe.lock(buffer);
    lock.lock();
    float[] output = (float[]) safe.getOutput();

    for (int f = 0; f < buffer.frames; f++) {
      Shape shape = getCurrentShape().setWeight(weight);

      double totalAudioFrames = shape.getWeight() * shape.getLength();
      double drawingProgress = totalAudioFrames == 0 ? 1 : audioFramesDrawn / totalAudioFrames;
      Vector2 nextVector = applyEffects(f, shape.nextVector(drawingProgress));

      float leftChannel = cutoff((float) nextVector.getX());
      float rightChannel = cutoff((float) nextVector.getY());

      output[f * NUM_OUTPUTS] = leftChannel;
      output[f * NUM_OUTPUTS + 1] = rightChannel;

      writeChannels(leftChannel, rightChannel);

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
    lock.unlock();
    return 0;
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

  private float cutoff(float value) {
    if (value < -1) {
      return -1;
    } else if (value > 1) {
      return 1;
    }
    return value;
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

    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtSystem system = platform.setupToSystem(XtSetup.SYSTEM_AUDIO);
      XtService service = getService(platform, system);
      String deviceId = getDeviceId(service);

      try (XtDevice device = service.openDevice(deviceId)) {
        // TODO: Make this generic to the type of XtSample of the current device.
        XtMix mix = new XtMix(sampleRate, XtSample.FLOAT32);
        XtChannels channels = new XtChannels(0, 0, NUM_OUTPUTS, 0);
        XtFormat format = new XtFormat(mix, channels);

        if (device.supportsFormat(format)) {
          XtBufferSize size = device.getBufferSize(format);
          XtStreamParams streamParams = new XtStreamParams(true, this::render, null, null);
          XtDeviceStreamParams deviceParams = new XtDeviceStreamParams(streamParams, format, size.current);

          try (XtStream stream = device.openStream(deviceParams, null);
               XtSafeBuffer safe = XtSafeBuffer.register(stream, true)) {
            stream.start();
            while (!stopped) {
              Thread.onSpinWait();
            }
            stream.stop();
          }
        } else {
          throw new RuntimeException("Audio device does not support audio format");
        }
      }
    }
  }

  private String getFirstDevice(XtService service) {
    return service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT)).getId(0);
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
      lock.lock();
      listeners.add(listener);
    } finally {
      lock.unlock();
    }
    listener.waitUntilFull();
    try {
      lock.lock();
      listeners.remove(listener);
    } finally {
      lock.unlock();
    }
  }

  @Override
  public void startRecord() {
    outputStream = new ByteArrayOutputStream();
    framesRecorded = 0;
    recording = true;
  }

  @Override
  public int samplesPerSecond() {
    return sampleRate;
  }

  @Override
  public AudioInputStream stopRecord() {
    recording = false;
    byte[] input = outputStream.toByteArray();
    outputStream = null;

    AudioFormat audioFormat = new AudioFormat(sampleRate, BITS_PER_SAMPLE, NUM_OUTPUTS, SIGNED, BIG_ENDIAN);

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
