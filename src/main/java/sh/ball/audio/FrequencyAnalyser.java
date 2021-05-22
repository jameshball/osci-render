package sh.ball.audio;

import sh.ball.audio.fft.FFTFactory;
import sh.ball.audio.fft.Transform;

import java.util.ArrayList;
import java.util.List;

public class FrequencyAnalyser<S, T> implements Runnable {

  private static final float NORMALIZATION_FACTOR_2_BYTES = Short.MAX_VALUE + 1.0f;

  private final Renderer<S, T> renderer;
  private final List<FrequencyListener> listeners = new ArrayList<>();
  private final int frameSize;
  private final int sampleRate;

  public FrequencyAnalyser(Renderer<S, T> renderer, int frameSize, int sampleRate) {
    this.renderer = renderer;
    this.frameSize = frameSize;
    this.sampleRate = sampleRate;
  }

  public void addListener(FrequencyListener listener) {
    listeners.add(listener);
  }

  private void notifyListeners(double frequency) {
    for (FrequencyListener listener : listeners) {
      listener.updateFrequency(frequency);
    }
  }

  // Adapted from https://stackoverflow.com/questions/53997426/java-how-to-get-current-frequency-of-audio-input
  @Override
  public void run() {
    final byte[] buf = new byte[2 << 16]; // <--- increase this for higher frequency resolution
    final int numberOfSamples = (buf.length / 2) / frameSize;
    Transform fft = FFTFactory.getInstance().create(numberOfSamples);

    int frequencyBin = sampleRate / (numberOfSamples / 2);

    while (true) {
      try {
        renderer.read(buf);
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
      // the stream represents each sample as two bytes -> decode
      final float[] samples = decode(buf);
      final float[][] transformed = fft.transform(samples);
      final float[] realPart = transformed[0];
      final float[] imaginaryPart = transformed[1];
      final double[] magnitudes = toMagnitudes(realPart, imaginaryPart);

      int maxIndex = 0;
      double max = Double.NEGATIVE_INFINITY;
      for (int i = 1; i < magnitudes.length / 2; i++) {
        if (i * frequencyBin < 20 || i * frequencyBin > 20000) {
          continue;
        }
        if (magnitudes[i] > max) {
          maxIndex = i;
          max = magnitudes[i];
        }
      }

      System.out.println((maxIndex + 1) * frequencyBin * 1.05);
    }
  }

  private float[] decode(final byte[] buf) {
    final float[] fbuf = new float[(buf.length / 2) / frameSize];
    int byteNum = 0;
    int i = 0;
    for (int pos = 0; pos < buf.length; pos += frameSize) {
      if (byteNum < 2) {
        int sample = byteToIntLittleEndian(buf, pos, frameSize);
        // normalize to [0,1] (not strictly necessary, but makes things easier)
        fbuf[i] = sample / NORMALIZATION_FACTOR_2_BYTES;
        i++;
      }
      byteNum++;
      byteNum %= 4;
    }
    return fbuf;
  }

  private double[] toMagnitudes(final float[] realPart, final float[] imaginaryPart) {
    final double[] powers = new double[realPart.length / 2];
    for (int i = 0; i < powers.length; i++) {
      powers[i] = Math.sqrt(realPart[i] * realPart[i] + imaginaryPart[i] * imaginaryPart[i]);
    }
    return powers;
  }

  private static int byteToIntLittleEndian(final byte[] buf, final int offset, final int bytesPerSample) {
    int sample = 0;
    for (int byteIndex = 0; byteIndex < bytesPerSample; byteIndex++) {
      final int aByte = buf[offset + byteIndex] & 0xff;
      sample += aByte << 8 * (byteIndex);
    }
    return sample;
  }
}
