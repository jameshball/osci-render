package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;
import xt.audio.*;

import java.util.EnumSet;
import java.util.concurrent.Callable;
import java.util.concurrent.locks.ReentrantLock;

public class XtAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;

  private final int sampleRate;

  private volatile boolean stopped = false;
  private ReentrantLock renderLock;
  private Callable<Vector2> channelGenerator;

  public XtAudioEngine() {
    try (XtPlatform platform = XtAudio.init(null, null)) {
      Enums.XtSystem system = platform.setupToSystem(Enums.XtSetup.SYSTEM_AUDIO);
      XtService service = getService(platform, system);
      String deviceId = getDeviceId(service);

      try (XtDevice device = service.openDevice(deviceId)) {
        // Gets the sample rate of the device. E.g. 192000 hz.
        this.sampleRate = device.getMix().map(xtMix -> xtMix.rate).orElse(DEFAULT_SAMPLE_RATE);
      }
    }
  }

  private int render(XtStream stream, Structs.XtBuffer buffer, Object user) throws Exception {
    XtSafeBuffer safe = XtSafeBuffer.get(stream);
    safe.lock(buffer);
    if (renderLock != null) {
      renderLock.lock();
    }
    float[] output = (float[]) safe.getOutput();

    for (int f = 0; f < buffer.frames; f++) {
      Vector2 channels = channelGenerator.call();
      output[f * NUM_OUTPUTS] = (float) channels.getX();
      output[f * NUM_OUTPUTS + 1] = (float) channels.getY();
    }
    safe.unlock(buffer);
    if (renderLock != null) {
      renderLock.unlock();
    }
    return 0;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, ReentrantLock renderLock) {
    this.channelGenerator = channelGenerator;
    this.renderLock = renderLock;
    try (XtPlatform platform = XtAudio.init(null, null)) {
      Enums.XtSystem system = platform.setupToSystem(Enums.XtSetup.SYSTEM_AUDIO);
      XtService service = getService(platform, system);
      String deviceId = getDeviceId(service);

      try (XtDevice device = service.openDevice(deviceId)) {
        // TODO: Make this generic to the type of XtSample of the current device.
        Structs.XtMix mix = new Structs.XtMix(sampleRate, Enums.XtSample.FLOAT32);
        Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
        Structs.XtFormat format = new Structs.XtFormat(mix, channels);

        if (device.supportsFormat(format)) {
          Structs.XtBufferSize size = device.getBufferSize(format);
          Structs.XtStreamParams streamParams = new Structs.XtStreamParams(true, this::render, null, null);
          Structs.XtDeviceStreamParams deviceParams = new Structs.XtDeviceStreamParams(streamParams, format, size.current);

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

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public int sampleRate() {
    return sampleRate;
  }

  private XtService getService(XtPlatform platform, Enums.XtSystem system) {
    XtService service = platform.getService(system);
    if (service == null) {
      service = platform.getService(platform.setupToSystem(Enums.XtSetup.PRO_AUDIO));
    }
    if (service == null) {
      service = platform.getService(platform.setupToSystem(Enums.XtSetup.CONSUMER_AUDIO));
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
      return getFirstDevice(service);
    }
    return deviceId;
  }

  private String getFirstDevice(XtService service) {
    return service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT)).getId(0);
  }
}
