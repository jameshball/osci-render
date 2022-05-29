package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import javax.sound.sampled.*;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.stream.Stream;

public class JavaAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // stereo audio
  private int NUM_CHANNELS = 4;
  private static final int LATENCY_MS = 30;
  private static final int MAX_FRAME_LATENCY = 512;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  private int FRAME_SIZE = NUM_CHANNELS * BIT_DEPTH / 8;
  private static final boolean BIG_ENDIAN = false;
  private static final boolean SIGNED_SAMPLE = true;

  private volatile boolean stopped = false;

  private SourceDataLine source;
  private AudioDevice device;

  @Override
  public boolean isPlaying() {
    return source.isRunning();
  }

  private int calculateBufferSize(AudioDevice device, int latencyMs) {
    return Math.max((int) (device.sampleRate() * latencyMs * 0.0005), MAX_FRAME_LATENCY) * FRAME_SIZE;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    this.stopped = false;
    this.device = device;
    AudioFormat format;

    while (true) {
      format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, NUM_CHANNELS, SIGNED_SAMPLE, BIG_ENDIAN);

      try {
        // connects to a device that can support the format above (i.e. default audio device)
        this.source = AudioSystem.getSourceDataLine(format);
        break;
      } catch (IllegalArgumentException e) {
        System.out.println(e.getMessage());
        NUM_CHANNELS--;
        FRAME_SIZE = NUM_CHANNELS * BIT_DEPTH / 8;
      }
    }

    int bufferSize = calculateBufferSize(device, LATENCY_MS);

    byte[] buffer = new byte[bufferSize * 2];
    short[] channels = new short[NUM_CHANNELS];
    Arrays.fill(channels, Short.MAX_VALUE);

    source.open(format, buffer.length);

    source.start();
    while (!stopped) {
      int requiredSamples = bufferSize / FRAME_SIZE;

      if (requiredSamples * NUM_CHANNELS > buffer.length / 2) {
        buffer = new byte[requiredSamples * NUM_CHANNELS * 2];
      }

      for (int i = 0; i < requiredSamples; i++) {
        try {
          Vector2 vector = channelGenerator.call();
          // converting doubles from Vector2 into shorts and then bytes so
          // that the byte buffer supports them
          if (NUM_CHANNELS > 0) {
            channels[0] = (short) (vector.getX() * Short.MAX_VALUE);
          }
          if (NUM_CHANNELS > 1) {
            channels[1] = (short) (vector.getY() * Short.MAX_VALUE);
          }

          for (int j = 0; j < NUM_CHANNELS; j++) {
            buffer[i * NUM_CHANNELS * 2 + j * 2] = (byte) channels[j];
            buffer[i * NUM_CHANNELS * 2 + j * 2 + 1] = (byte) (channels[j] >> 8);
          }
        } catch (Exception e) {
          e.printStackTrace();
        }
      }

      source.write(buffer, 0, requiredSamples * FRAME_SIZE);
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
    return Stream.of(44100, 48000, 96000, 192000).map(rate ->
      (AudioDevice) new SimpleAudioDevice("default-" + rate, "default", rate, AudioSample.INT16)).toList();
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return new SimpleAudioDevice("default", "default", DEFAULT_SAMPLE_RATE, AudioSample.INT16);
  }

  @Override
  public AudioDevice currentDevice() {
    return device;
  }
}
