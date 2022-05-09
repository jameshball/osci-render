package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;
import xt.audio.*;

import java.util.ArrayList;
import java.util.EnumSet;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.Callable;

// Audio engine that connects to devices using the XtAudio library
public class XtAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  private static final Enums.XtSample DEFAULT_AUDIO_SAMPLE = Enums.XtSample.FLOAT32;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;

  private volatile boolean stopped = false;

  private AudioDevice device;
  private boolean playing = false;
  private Callable<Vector2> channelGenerator;

  public XtAudioEngine() {}

  private int render(XtStream stream, Structs.XtBuffer buffer, Object user) throws Exception {
    XtSafeBuffer safe = XtSafeBuffer.get(stream);
    safe.lock(buffer);
    Object output = safe.getOutput();

    for (int f = 0; f < buffer.frames; f++) {
      Vector2 channels = channelGenerator.call();
      writeChannels(channels, output, f);
    }
    safe.unlock(buffer);
    return 0;
  }

  // fully compatible function to write a Vector2 (i.e. stereo) channels to
  // any AudioSample. This ensures maximum compatibility for audio devices.
  private void writeChannels(Vector2 channels, Object output, int frame) {
    int index = frame * NUM_OUTPUTS;
    switch (device.sample()) {
      case UINT8 -> {
        byte[] byteOutput = (byte[]) output;
        byteOutput[index] = (byte) ((int) (128 * (channels.getX() + 1)));
        byteOutput[index + 1] = (byte) ((int) (128 * (channels.getY() + 1)));
      }
      case INT8 -> {
        byte[] byteOutput = (byte[]) output;
        byteOutput[index] = (byte) ((int) (128 * channels.getX()));
        byteOutput[index + 1] = (byte) ((int) (128 * channels.getY()));
      }
      case INT16 -> {
        short[] shortOutput = (short[]) output;
        shortOutput[index] = (short) (channels.getX() * Short.MAX_VALUE);
        shortOutput[index + 1] = (short) (channels.getY() * Short.MAX_VALUE);
      }
      case INT24 -> {
        index *= 3;
        byte[] byteOutput = (byte[]) output;
        int leftChannel = (int) (channels.getX() * 8388607);
        int rightChannel = (int) (channels.getY() * 8388607);
        byteOutput[index] = (byte) leftChannel;
        byteOutput[index + 1] = (byte) (leftChannel >> 8);
        byteOutput[index + 2] = (byte) (leftChannel >> 16);
        byteOutput[index + 3] = (byte) rightChannel;
        byteOutput[index + 4] = (byte) (rightChannel >> 8);
        byteOutput[index + 5] = (byte) (rightChannel >> 16);
      }
      case INT32 -> {
        int[] intOutput = (int[]) output;
        intOutput[index] = (int) (channels.getX() * Integer.MAX_VALUE);
        intOutput[index + 1] = (int) (channels.getY() * Integer.MAX_VALUE);
      }
      case FLOAT32 -> {
        float[] floatOutput = (float[]) output;
        floatOutput[index] = (float) channels.getX();
        floatOutput[index + 1] = (float) channels.getY();
      }
      case FLOAT64 -> {
        double[] doubleOutput = (double[]) output;
        doubleOutput[index] = channels.getX();
        doubleOutput[index + 1] = channels.getY();
      }
    }
  }

  @Override
  public boolean isPlaying() {
    return playing;
  }

  // XtAudio boilerplate for connecting to an audio device and playing audio
  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) {
    this.playing = true;
    this.device = device;
    this.channelGenerator = channelGenerator;
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);

      try (XtDevice xtDevice = service.openDevice(device.id())) {
        Structs.XtMix mix = new Structs.XtMix(device.sampleRate(), AudioSampleToXtSample(device.sample()));
        Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
        Structs.XtFormat format = new Structs.XtFormat(mix, channels);

        if (xtDevice.supportsFormat(format)) {
          Structs.XtBufferSize size = xtDevice.getBufferSize(format);
          Structs.XtStreamParams streamParams = new Structs.XtStreamParams(true, this::render, null, null);
          Structs.XtDeviceStreamParams deviceParams = new Structs.XtDeviceStreamParams(streamParams, format, size.current);

          try (XtStream stream = xtDevice.openStream(deviceParams, null);
               XtSafeBuffer safe = XtSafeBuffer.register(stream)) {
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
    playing = false;
    this.device = null;
  }

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public void setAudioStability(boolean stable) {
    // TODO: unimplemented
  }

  // XtAudio boilerplate for getting a list of connected audio devices
  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      XtDeviceList xtDevices = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT));

      for (int i = 0; i < xtDevices.getCount(); i++) {
        String deviceId = xtDevices.getId(i);
        String deviceName = xtDevices.getName(deviceId);

        if (deviceName.startsWith("null")) {
          continue;
        }

        try (XtDevice xtDevice = service.openDevice(deviceId)) {
          Optional<Structs.XtMix> mix = xtDevice.getMix();

          if (mix.isEmpty()) {
            mix = Optional.of(new Structs.XtMix(DEFAULT_SAMPLE_RATE, DEFAULT_AUDIO_SAMPLE));
          }

          Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
          Structs.XtFormat format = new Structs.XtFormat(mix.get(), channels);

          if (xtDevice.supportsFormat(format)) {
            devices.add(new SimpleAudioDevice(deviceId, deviceName, mix.get().rate, XtSampleToAudioSample(mix.get().sample)));
          }
        } catch (XtException e) {
          e.printStackTrace();
        }
      }
    }

    if (devices.isEmpty()) {
      throw new RuntimeException("No suitable audio devices found");
    }

    return devices;
  }

  // XtAudio boilerplate for getting default device
  @Override
  public AudioDevice getDefaultDevice() {
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      String deviceId = service.getDefaultDeviceId(true);

      try (XtDevice xtDevice = service.openDevice(deviceId)) {
        Optional<Structs.XtMix> mix = xtDevice.getMix();

        if (mix.isEmpty()) {
          mix = Optional.of(new Structs.XtMix(DEFAULT_SAMPLE_RATE, DEFAULT_AUDIO_SAMPLE));
        }

        String deviceName = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT)).getName(deviceId);

        Structs.XtChannels channels = new Structs.XtChannels(0, 0, NUM_OUTPUTS, 0);
        Structs.XtFormat format = new Structs.XtFormat(mix.get(), channels);

        if (xtDevice.supportsFormat(format)) {
          return new SimpleAudioDevice(deviceId, deviceName, mix.get().rate, XtSampleToAudioSample(mix.get().sample));
        } else {
          return null;
        }
      }
    }
  }

  @Override
  public AudioDevice currentDevice() {
    return device;
  }


  // connects to an XtAudio XtService in order of lowest latency to highest latency
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

  // helper for converting XtSamples into AudioSamples
  private AudioSample XtSampleToAudioSample(Enums.XtSample sample) {
    return switch (sample) {
      case UINT8 -> AudioSample.UINT8;
      case INT16 -> AudioSample.INT16;
      case INT24 -> AudioSample.INT24;
      case INT32 -> AudioSample.INT32;
      case FLOAT32 -> AudioSample.FLOAT32;
    };
  }

  // helper for converting AudioSamples into XtSamples
  private Enums.XtSample AudioSampleToXtSample(AudioSample sample) {
    return switch (sample) {
      case UINT8 -> Enums.XtSample.UINT8;
      case INT16 -> Enums.XtSample.INT16;
      case INT24 -> Enums.XtSample.INT24;
      case INT32 -> Enums.XtSample.INT32;
      case FLOAT32 -> Enums.XtSample.FLOAT32;
      case INT8, FLOAT64 -> throw new UnsupportedOperationException("Unsupported sample format");
    };
  }
}
