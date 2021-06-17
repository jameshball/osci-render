package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;
import xt.audio.*;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.Callable;
import java.util.concurrent.locks.ReentrantLock;

public class XtAudioEngine implements AudioEngine {

  // Stereo audio
  private static final int NUM_OUTPUTS = 2;

  private volatile boolean stopped = false;
  private ReentrantLock renderLock;
  private Callable<Vector2> channelGenerator;

  public XtAudioEngine() {}

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
  public void play(Callable<Vector2> channelGenerator, ReentrantLock renderLock, AudioDevice device) {
    this.channelGenerator = channelGenerator;
    this.renderLock = renderLock;
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);

      try (XtDevice xtDevice = service.openDevice(device.name())) {
        // TODO: Make this generic to the type of XtSample of the current device.
        Structs.XtMix mix = new Structs.XtMix(xtDevice.getMix().orElseThrow().rate, Enums.XtSample.FLOAT32);
        Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
        Structs.XtFormat format = new Structs.XtFormat(mix, channels);

        if (xtDevice.supportsFormat(format)) {
          Structs.XtBufferSize size = xtDevice.getBufferSize(format);
          Structs.XtStreamParams streamParams = new Structs.XtStreamParams(true, this::render, null, null);
          Structs.XtDeviceStreamParams deviceParams = new Structs.XtDeviceStreamParams(streamParams, format, size.current);

          try (XtStream stream = xtDevice.openStream(deviceParams, null);
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
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      XtDeviceList xtDevices = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT));

      for (int i = 0; i < xtDevices.getCount(); i++) {
        String device = xtDevices.getId(i);

        try (XtDevice xtDevice = service.openDevice(device)) {
          Optional<Structs.XtMix> mix = xtDevice.getMix();

          if (mix.isEmpty()) {
            continue;
          }

          Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
          Structs.XtFormat format = new Structs.XtFormat(mix.get(), channels);

          if (xtDevice.supportsFormat(format)) {
            devices.add(new DefaultAudioDevice(device, mix.get().rate, XtSampleToAudioSample(mix.get().sample)));
          }
        }
      }
    }

    return devices;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      String device = service.getDefaultDeviceId(true);

      try (XtDevice xtDevice = service.openDevice(device)) {
        Optional<Structs.XtMix> mix = xtDevice.getMix();

        if (mix.isEmpty()) {
          return null;
        }

        Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
        Structs.XtFormat format = new Structs.XtFormat(mix.get(), channels);

        if (xtDevice.supportsFormat(format)) {
          return new DefaultAudioDevice(device, mix.get().rate, XtSampleToAudioSample(mix.get().sample));
        } else {
          return null;
        }
      }
    }
  }


  private XtService getService(XtPlatform platform) {
    XtService service = platform.getService(platform.setupToSystem(Enums.XtSetup.SYSTEM_AUDIO));
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

  private AudioSample XtSampleToAudioSample(Enums.XtSample sample) {
    return switch (sample) {
      case UINT8 -> AudioSample.UINT8;
      case INT16 -> AudioSample.INT16;
      case INT24 -> AudioSample.INT24;
      case INT32 -> AudioSample.INT32;
      case FLOAT32 -> AudioSample.FLOAT32;
    };
  }
}
