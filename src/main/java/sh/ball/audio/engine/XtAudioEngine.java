package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;
import xt.audio.*;

import java.util.*;
import java.util.concurrent.Callable;
import java.util.function.Predicate;
import java.util.logging.Level;
import java.util.stream.Stream;

import static sh.ball.gui.Gui.logger;

// Audio engine that connects to devices using the XtAudio library
public class XtAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  private static final int DEFAULT_NUM_CHANNELS = 2;
  private static final Enums.XtSample DEFAULT_AUDIO_SAMPLE = Enums.XtSample.FLOAT32;

  private volatile boolean stopped = false;

  private AudioDevice outputDevice;
  private boolean playing = false;
  private Callable<Vector2> channelGenerator;
  private double brightness = 1.0;

  // microphone
  private final List<AudioInputListener> listeners = new ArrayList<>();
  private AudioDevice inputDevice;

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
  private void writeChannels(Vector2 vector, Object output, int frame) {
    int index = frame * outputDevice.channels();
    double[] channels = new double[outputDevice.channels()];
    Arrays.fill(channels, brightness);

    if (channels.length > 0) {
      channels[0] = vector.getX();
    }
    if (channels.length > 1) {
      channels[1] = vector.getY();
    }

    switch (outputDevice.sample()) {
      case UINT8 -> {
        byte[] byteOutput = (byte[]) output;
        for (int i = 0; i < channels.length; i++) {
          byteOutput[index + i] = (byte) ((int) (128 * (channels[i] + 1)));
        }
      }
      case INT8 -> {
        byte[] byteOutput = (byte[]) output;
        for (int i = 0; i < channels.length; i++) {
          byteOutput[index + i] = (byte) ((int) (128 * channels[i]));
        }
      }
      case INT16 -> {
        short[] shortOutput = (short[]) output;
        for (int i = 0; i < channels.length; i++) {
          shortOutput[index + i] = (short) (channels[i] * Short.MAX_VALUE);
        }
      }
      case INT24 -> {
        index *= 3;
        byte[] byteOutput = (byte[]) output;
        for (int i = 0; i < channels.length; i++) {
          int channel = (int) (channels[i] * 8388607);
          byteOutput[index + 3 * i] = (byte) channel;
          byteOutput[index + 3 * i + 1] = (byte) (channel >> 8);
          byteOutput[index + 3 * i + 2] = (byte) (channel >> 16);
        }
      }
      case INT32 -> {
        int[] intOutput = (int[]) output;
        for (int i = 0; i < channels.length; i++) {
          intOutput[index + i] = (int) (channels[i] * Integer.MAX_VALUE);
        }
      }
      case FLOAT32 -> {
        float[] floatOutput = (float[]) output;
        for (int i = 0; i < channels.length; i++) {
          floatOutput[index + i] = (float) channels[i];
        }
      }
      case FLOAT64 -> {
        double[] doubleOutput = (double[]) output;
        for (int i = 0; i < channels.length; i++) {
          doubleOutput[index + i] = channels[i];
        }
      }
    }
  }

  private void readChannels(Object input, double[] samples, int frame) {
    int channels = inputDevice.channels();
    int index = frame * channels;

    switch (inputDevice.sample()) {
      case UINT8 -> {
        byte[] byteInput = (byte[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = (byteInput[index + i] - 128) / 128.0;
        }
      }
      case INT8 -> {
        byte[] byteInput = (byte[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = byteInput[index + i] / 128.0;
        }
      }
      case INT16 -> {
        short[] shortInput = (short[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = shortInput[index + i] / (double) Short.MAX_VALUE;
        }
      }
      case INT24 -> {
        index *= 3;
        byte[] byteInput = (byte[]) input;
        for (int i = 0; i < channels; i++) {
          int channel = byteInput[index + 3 * i] & 0xFF;
          channel |= (byteInput[index + 3 * i + 1] & 0xFF) << 8;
          channel |= (byteInput[index + 3 * i + 2] & 0xFF) << 16;
          samples[index + i] = channel / 8388607.0;
        }
      }
      case INT32 -> {
        int[] intInput = (int[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = intInput[index + i] / (double) Integer.MAX_VALUE;
        }
      }
      case FLOAT32 -> {
        float[] floatInput = (float[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = floatInput[index + i];
        }
      }
      case FLOAT64 -> {
        double[] doubleInput = (double[]) input;
        for (int i = 0; i < channels; i++) {
          samples[index + i] = doubleInput[index + i];
        }
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
    this.outputDevice = device;
    this.channelGenerator = channelGenerator;
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      if (service == null) {
        return;
      }

      try (XtDevice xtDevice = service.openDevice(device.id())) {
        Structs.XtMix mix = new Structs.XtMix(device.sampleRate(), AudioSampleToXtSample(device.sample()));
        Structs.XtChannels channels = new Structs.XtChannels(0, 0, device.channels(), 0);
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
    this.outputDevice = null;
  }

  @Override
  public void stop() {
    stopped = true;
  }

  private List<AudioDevice> getDevices(XtService service, XtDeviceList xtDevices, boolean output) {
    List<AudioDevice> devices = new ArrayList<>();

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

        Optional<Structs.XtMix> finalMix = mix;
        Stream.of(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12).forEach(numChannels -> {
          Structs.XtChannels channels = null;
          if (output) {
            channels = new Structs.XtChannels(0, 0, numChannels, 0);
          } else {
            channels = new Structs.XtChannels(numChannels, 0, 0, 0);
          }
          Structs.XtFormat format = new Structs.XtFormat(finalMix.get(), channels);

          if (xtDevice.supportsFormat(format)) {
            devices.add(new SimpleAudioDevice(deviceId, deviceName, finalMix.get().rate, XtSampleToAudioSample(finalMix.get().sample), numChannels, null, output));
          }
        });
      } catch (XtException e) {
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
    }

    return devices;
  }

  // XtAudio boilerplate for getting a list of connected audio devices
  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      if (service == null) {
        return devices;
      }

      try (XtDeviceList inputs = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.INPUT))) {
        devices.addAll(getDevices(service, inputs, false));
      }

      try (XtDeviceList outputs = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.OUTPUT))) {
        devices.addAll(getDevices(service, outputs, true));
      }
    }

    if (devices.isEmpty()) {
      throw new RuntimeException("No suitable audio devices found");
    }

    return devices;
  }

  private AudioDevice getDefaultDevice(boolean output) {
    try (XtPlatform platform = XtAudio.init(null, null)) {
      XtService service = getService(platform);
      if (service == null) {
        return null;
      }
      String deviceId = service.getDefaultDeviceId(output);

      try (XtDevice xtDevice = service.openDevice(deviceId)) {
        Optional<Structs.XtMix> mix = xtDevice.getMix();

        if (mix.isEmpty()) {
          mix = Optional.of(new Structs.XtMix(DEFAULT_SAMPLE_RATE, DEFAULT_AUDIO_SAMPLE));
        }

        String deviceName;
        try (XtDeviceList xtDevices = service.openDeviceList(EnumSet.of(Enums.XtEnumFlags.ALL))) {
          deviceName = xtDevices.getName(deviceId);
        }
        Structs.XtChannels channels;
        if (output) {
          channels = new Structs.XtChannels(0, 0, DEFAULT_NUM_CHANNELS, 0);
        } else {
          channels = new Structs.XtChannels(DEFAULT_NUM_CHANNELS, 0, 0, 0);
        }
        Structs.XtFormat format = new Structs.XtFormat(mix.get(), channels);

        if (xtDevice.supportsFormat(format)) {
          return new SimpleAudioDevice(deviceId, deviceName, mix.get().rate, XtSampleToAudioSample(mix.get().sample), DEFAULT_NUM_CHANNELS, null, output);
        } else {
          return null;
        }
      }
    }
  }

  @Override
  public AudioDevice getDefaultOutputDevice() {
    return getDefaultDevice(true);
  }

  @Override
  public AudioDevice getDefaultInputDevice() {
    return getDefaultDevice(false);
  }

  @Override
  public AudioDevice currentOutputDevice() {
    return outputDevice;
  }

  @Override
  public AudioDevice currentInputDevice() {
    return inputDevice;
  }

  @Override
  public void setBrightness(double brightness) {
    this.brightness = brightness;
  }

  @Override
  public void addListener(AudioInputListener listener) {
    listeners.add(listener);
  }

  @Override
  public void listen(AudioDevice device) {
    Structs.XtStreamParams streamParams;
    Structs.XtDeviceStreamParams deviceParams;

    this.inputDevice = device;

    try(XtPlatform platform = XtAudio.init(null, null)) {
      Enums.XtSystem system = platform.setupToSystem(Enums.XtSetup.CONSUMER_AUDIO);
      XtService service = platform.getService(system);
      if(service == null) return;

      try(XtDevice xtDevice = service.openDevice(device.id())) {
        Structs.XtMix mix = new Structs.XtMix(device.sampleRate(), AudioSampleToXtSample(device.sample()));
        Structs.XtChannels channels = new Structs.XtChannels(0, 0, device.channels(), 0);
        Structs.XtFormat format = new Structs.XtFormat(mix, channels);

        if(xtDevice.supportsFormat(format)) {
          Structs.XtBufferSize size = xtDevice.getBufferSize(format);
          streamParams = new Structs.XtStreamParams(true, this::readInput, null, null);
          deviceParams = new Structs.XtDeviceStreamParams(streamParams, format, size.current);


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

    this.inputDevice = null;
  }

  private int readInput(XtStream stream, Structs.XtBuffer buffer, Object user) throws Exception {
    XtSafeBuffer safe = XtSafeBuffer.get(stream);
    safe.lock(buffer);

    Object input = safe.getInput();
    double[] samples = new double[buffer.frames * inputDevice.channels()];

    for (int f = 0; f < buffer.frames; f++) {
      readChannels(input, samples, f);
    }

    for (AudioInputListener listener : listeners) {
      listener.transmit(samples);
    }

    safe.unlock(buffer);
    return 0;
  }

  @Override
  public boolean inputAvailable() {
    return devices().stream().anyMatch(Predicate.not(AudioDevice::output));
  }


  // connects to an XtAudio XtService in order of lowest latency to highest latency
  private XtService getService(XtPlatform platform) {
    XtService service = platform.getService(Enums.XtSystem.JACK);
    if (service == null) {
      logger.log(Level.SEVERE, "No XtAudio service found");
    } else if (service.getCapabilities().contains(Enums.XtServiceCaps.NONE)) {
      logger.log(Level.SEVERE, "Audio service has no capabilities");
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
