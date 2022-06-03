package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.stream.Stream;

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

  private volatile boolean stopped = false;

  private SourceDataLine source;
  private AudioDevice device;
  private double brightness = 1.0;

  @Override
  public boolean isPlaying() {
    return source.isRunning();
  }

  private int calculateBufferSize(AudioDevice device, int frameSize, int latencyMs) {
    return Math.max((int) (device.sampleRate() * latencyMs * 0.0005), MAX_FRAME_LATENCY) * frameSize;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    this.device = device;

    AudioFormat format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, device.channels(), SIGNED_SAMPLE, BIG_ENDIAN);
    this.source = AudioSystem.getSourceDataLine(format);

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
          e.printStackTrace();
        }
      }

      source.write(buffer, 0, requiredSamples * frameSize);
    }
    source.stop();
    this.device = null;
  }

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    Stream.of(192000, 96000, 48000, 44100).forEach(rate ->
      Stream.of(1, 2, 3, 4, 6, 8).forEach(channels -> {
        try {
          AudioFormat format = new AudioFormat((float) rate, BIT_DEPTH, channels, SIGNED_SAMPLE, BIG_ENDIAN);
          this.source = AudioSystem.getSourceDataLine(format);
          devices.add(new SimpleAudioDevice("default-" + rate, "default", rate, AudioSample.INT16, channels));
        } catch (Exception ignored) {}
      })
    );

    return devices;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return new SimpleAudioDevice("default", "default", DEFAULT_SAMPLE_RATE, AudioSample.INT16, DEFAULT_NUM_CHANNELS);
  }

  @Override
  public AudioDevice currentDevice() {
    return device;
  }

  @Override
  public void setBrightness(double brightness) {
    this.brightness = brightness;
  }
}
