package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioSystem;
import javax.sound.sampled.SourceDataLine;
import java.util.List;
import java.util.concurrent.Callable;

public class JavaAudioEngine implements AudioEngine {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // stereo audio
  private static final int NUM_CHANNELS = 2;
  private static final int LATENCY_MS = 10;
  private static final int MAX_FRAME_LATENCY = 512;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  private static final int FRAME_SIZE = NUM_CHANNELS * BIT_DEPTH / 8;
  private static final boolean BIG_ENDIAN = false;
  private static final boolean SIGNED_SAMPLE = true;

  private volatile boolean stopped = false;

  private SourceDataLine source;
  private AudioDevice device;

  @Override
  public boolean isPlaying() {
    return source.isRunning();
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    this.stopped = false;
    this.device = device;

    AudioFormat format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, NUM_CHANNELS, SIGNED_SAMPLE, BIG_ENDIAN);

    // connects to a device that can support the format above (i.e. default audio device)
    this.source = AudioSystem.getSourceDataLine(format);
    source.open(format);

    int frameLatency = Math.max((int) (device.sampleRate() * LATENCY_MS * 0.0005), MAX_FRAME_LATENCY);
    int bufferSize = frameLatency * FRAME_SIZE;
    int remainingBufferSpace = source.getBufferSize() - bufferSize;

    byte[] buffer = new byte[bufferSize * 2];

    source.start();
    while (!stopped) {
      int delta = source.available() - remainingBufferSpace;
      if (delta > 0) {
        int requiredSamples = (delta + bufferSize) / FRAME_SIZE;

        if (requiredSamples * NUM_CHANNELS > buffer.length / 2) {
          buffer = new byte[requiredSamples * NUM_CHANNELS * 2];
        }

        for (int i = 0; i < requiredSamples; i++) {
          try {
            Vector2 channels = channelGenerator.call();
            // converting doubles from Vector2 into shorts and then bytes so
            // that the byte buffer supports them
            short left = (short) (channels.getX() * Short.MAX_VALUE);
            short right = (short) (channels.getY() * Short.MAX_VALUE);
            buffer[i * 4] = (byte) left;
            buffer[i * 4 + 1] = (byte) (left >> 8);
            buffer[i * 4 + 2] = (byte) right;
            buffer[i * 4 + 3] = (byte) (right >> 8);
          } catch (Exception e) {
            e.printStackTrace();
          }
        }

        source.write(buffer, 0, requiredSamples * FRAME_SIZE);
      }
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
    return List.of(getDefaultDevice());
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
