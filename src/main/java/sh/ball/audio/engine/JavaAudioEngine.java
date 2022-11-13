package sh.ball.audio.engine;

import be.tarsos.dsp.resample.Resampler;
import sh.ball.shapes.Vector2;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.function.Predicate;
import java.util.logging.Level;
import java.util.stream.Stream;

import static sh.ball.gui.Gui.logger;

public class JavaAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // stereo audio
  private static final int DEFAULT_NUM_CHANNELS = 2;
  private static final int LATENCY_MS = 30;
  private static final int MAX_FRAME_LATENCY = 512;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  private static final boolean BIG_ENDIAN = false;
  private static final boolean SIGNED_SAMPLE = true;
  private static final int MAX_RESAMPLE_FACTOR = 10;

  private volatile boolean stopped = false;

  private SourceDataLine source;
  private AudioDevice outputDevice;
  private double brightness = 1.0;

  // microphone
  private static final AudioFormat inputFormat = new AudioFormat(DEFAULT_SAMPLE_RATE, BIT_DEPTH, DEFAULT_NUM_CHANNELS, SIGNED_SAMPLE, BIG_ENDIAN);
  private final List<AudioInputListener> listeners = new ArrayList<>();

  private TargetDataLine microphone;
  private AudioDevice inputDevice;
  private int sampleRate = DEFAULT_SAMPLE_RATE;

  @Override
  public boolean isPlaying() {
    return source.isRunning();
  }

  private int calculateBufferSize(AudioDevice device, int frameSize, int latencyMs) {
    return Math.max((int) (device.sampleRate() * latencyMs * 0.0005), MAX_FRAME_LATENCY) * frameSize;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    if (!device.output()) {
      throw new RuntimeException("Device is not an output device");
    }
    this.outputDevice = device;

    AudioFormat format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, device.channels(), SIGNED_SAMPLE, BIG_ENDIAN);
    if (device.mixerInfo() != null) {
      this.source = AudioSystem.getSourceDataLine(format, device.mixerInfo());
    } else {
      this.source = AudioSystem.getSourceDataLine(format);
    }

    int frameSize = device.channels() * BIT_DEPTH / 8;

    int bufferSize = calculateBufferSize(device, frameSize, LATENCY_MS);

    byte[] buffer = new byte[bufferSize * 2];
    short[] channels = new short[device.channels()];

    source.open(format, buffer.length);

    source.start();
    this.stopped = false;
    while (!stopped) {
      int requiredSamples = bufferSize / frameSize;

      if (requiredSamples * channels.length > buffer.length / 2) {
        buffer = new byte[requiredSamples * channels.length * 2];
      }

      for (int i = 0; i < requiredSamples; i++) {
        try {
          Arrays.fill(channels, (short) (Short.MAX_VALUE * brightness));
          Vector2 vector = channelGenerator.call();
          // converting doubles from Vector2 into shorts and then bytes so
          // that the byte buffer supports them
          if (channels.length > 0) {
            channels[0] = (short) (vector.getX() * Short.MAX_VALUE);
          }
          if (channels.length > 1) {
            channels[1] = (short) (vector.getY() * Short.MAX_VALUE);
          }

          for (int j = 0; j < channels.length; j++) {
            buffer[i * channels.length * 2 + j * 2] = (byte) channels[j];
            buffer[i * channels.length * 2 + j * 2 + 1] = (byte) (channels[j] >> 8);
          }
        } catch (Exception e) {
          logger.log(Level.SEVERE, e.getMessage(), e);
        }
      }

      source.write(buffer, 0, requiredSamples * frameSize);
    }
    source.stop();
    this.outputDevice = null;
  }

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    Line.Info audioOutput = new Line.Info(SourceDataLine.class);
    List<Mixer.Info> outputDeviceInfos = new ArrayList<>();

    Line.Info audioInput = new Line.Info(TargetDataLine.class);
    List<Mixer.Info> inputDeviceInfos = new ArrayList<>();

    Mixer.Info[] infos = AudioSystem.getMixerInfo();
    for (Mixer.Info info : infos) {
      Mixer mixer = AudioSystem.getMixer(info);
      if (mixer.isLineSupported(audioOutput)) {
        outputDeviceInfos.add(info);
      }
      if (mixer.isLineSupported(audioInput)) {
        inputDeviceInfos.add(info);
      }
    }

    for (Mixer.Info info : outputDeviceInfos) {
      Stream.of(192000, 96000, 48000, 44100).forEach(rate ->
        Stream.of(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12).forEach(channels -> {
          try {
            AudioFormat format = new AudioFormat((float) rate, BIT_DEPTH, channels, SIGNED_SAMPLE, BIG_ENDIAN);
            AudioSystem.getSourceDataLine(format, info);
            devices.add(new SimpleAudioDevice(info.getName() + "-" + rate, info.getName(), rate, AudioSample.INT16, channels, info, true));
          } catch (Exception ignored) {}
        })
      );
    }

    for (Mixer.Info info : inputDeviceInfos) {
      Stream.of(192000, 96000, 48000, 44100).forEach(rate ->
        Stream.of(1, 2).forEach(channels -> {
          try {
            AudioFormat format = new AudioFormat((float) rate, BIT_DEPTH, channels, SIGNED_SAMPLE, BIG_ENDIAN);
            AudioSystem.getTargetDataLine(format, info);
            devices.add(new SimpleAudioDevice(info.getName() + "-" + rate, info.getName(), rate, AudioSample.INT16, channels, info, false));
          } catch (Exception ignored) {}
        })
      );
    }

    return devices;
  }

  @Override
  public AudioDevice getDefaultOutputDevice() {
    return new SimpleAudioDevice("default", "default", DEFAULT_SAMPLE_RATE, AudioSample.INT16, DEFAULT_NUM_CHANNELS, null, true);
  }

  @Override
  public AudioDevice getDefaultInputDevice() {
    return new SimpleAudioDevice("default", "default", DEFAULT_SAMPLE_RATE, AudioSample.INT16, DEFAULT_NUM_CHANNELS, null, false);
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
    if (device.output()) {
      throw new RuntimeException("Device is not an input device");
    }

    this.inputDevice = device;

    Resampler r = new Resampler(false, 0.1, 10.0);

    try {
      AudioFormat format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, device.channels(), SIGNED_SAMPLE, BIG_ENDIAN);
      if (device.mixerInfo() != null) {
        this.microphone = AudioSystem.getTargetDataLine(format, device.mixerInfo());
      } else {
        this.microphone = AudioSystem.getTargetDataLine(format);
      }

      microphone.open(format);

      int frameSize = device.channels() * BIT_DEPTH / 8;

      int bufferSize = calculateBufferSize(device, frameSize, LATENCY_MS);

      byte[] buffer = new byte[bufferSize];
      int channelScalar = device.channels() == 1 ? 2 : 1;
      float[] floatSrcLeft = new float[channelScalar * buffer.length / 4];
      float[] floatSrcRight = new float[channelScalar * buffer.length / 4];
      float[] floatOutLeft = new float[channelScalar * MAX_RESAMPLE_FACTOR * buffer.length / 4];
      float[] floatOutRight = new float[channelScalar * MAX_RESAMPLE_FACTOR * buffer.length / 4];

      microphone.start();
      while (!stopped) {
        float resampleFactor = (float) sampleRate / (float) device.sampleRate();

        int outLength = channelScalar * (int) (resampleFactor * buffer.length / 2);
        if (outLength % 2 != 0) {
          outLength--;
        }
        double[] out = new double[outLength];
        microphone.read(buffer, 0, buffer.length);

        for (int i = 0; i < buffer.length - 3; i += 4) {
          float left = (float) ((buffer[i] & 0xFF) | (buffer[i + 1] << 8)) / 32768.0f;
          float right = (float) ((buffer[i + 2] & 0xFF) | (buffer[i + 3] << 8)) / 32768.0f;
          if (device.channels() == 1) {
            floatSrcLeft[i / 2] = left;
            floatSrcLeft[i / 2 + 1] = left;
            floatSrcRight[i / 2] = right;
            floatSrcRight[i / 2 + 1] = right;
          } else {
            floatSrcLeft[i / 4] = left;
            floatSrcRight[i / 4] = right;
          }
        }

        r.process(resampleFactor, floatSrcLeft, 0, floatSrcLeft.length, true, floatOutLeft, 0, out.length / 2);
        r.process(resampleFactor, floatSrcRight, 0, floatSrcRight.length, true, floatOutRight, 0, out.length / 2);
        for (int i = 0; i < out.length; i += 2) {
          out[i] = floatOutLeft[i / 2];
          out[i + 1] = floatOutRight[i / 2];
        }

        for (AudioInputListener listener : listeners) {
          listener.transmit(out);
        }
      }
      microphone.close();
    } catch (LineUnavailableException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }

    this.inputDevice = null;
  }

  @Override
  public boolean inputAvailable() {
    return devices().stream().anyMatch(Predicate.not(AudioDevice::output));
  }

  @Override
  public boolean isListening() {
    return microphone.isRunning();
  }

  @Override
  public void setSampleRate(int sampleRate) {
    this.sampleRate = sampleRate;
  }
}
