package sh.ball.audio;

import sh.ball.audio.fft.FFT;

import java.util.ArrayList;
import java.util.List;

public class FrequencyAnalyser<S, T> implements Runnable {

  private static final float NORMALIZATION_FACTOR_2_BYTES = Short.MAX_VALUE + 1.0f;
  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // increase this for higher frequency resolution, but less frequent frequency calculation
  private static final int DEFAULT_POWER_OF_TWO = 18;

  private final Renderer<S, T> renderer;
  private final List<FrequencyListener> listeners = new ArrayList<>();
  private final int frameSize;
  private final int sampleRate;
  private final int powerOfTwo;

  public FrequencyAnalyser(Renderer<S, T> renderer, int frameSize, int sampleRate) {
    this.renderer = renderer;
    this.frameSize = frameSize;
    this.sampleRate = sampleRate;
    this.powerOfTwo = (int) (DEFAULT_POWER_OF_TWO - Math.log(DEFAULT_SAMPLE_RATE / sampleRate) / Math.log(2));
  }

  public void addListener(FrequencyListener listener) {
    listeners.add(listener);
  }

  private void notifyListeners(double leftFrequency, double rightFrequency) {
    for (FrequencyListener listener : listeners) {
      listener.updateFrequency(leftFrequency, rightFrequency);
    }
  }

  // Adapted from https://stackoverflow.com/questions/53997426/java-how-to-get-current-frequency-of-audio-input
  @Override
  public void run() {
    byte[] buf = new byte[2 << powerOfTwo];

    while (true) {
      try {
        renderer.read(buf);
      } catch (InterruptedException e) {
        e.printStackTrace();
      }
      double[] leftSamples = decode(buf, true);
      double[] rightSamples = decode(buf, false);

      FFT leftFft = new FFT(leftSamples, null, false, true);
      FFT rightFft = new FFT(rightSamples, null, false, true);

      double[] leftMags = leftFft.getMagnitudeSpectrum();
      double[] rightMags = rightFft.getMagnitudeSpectrum();
      double[] bins = leftFft.getBinLabels(sampleRate);

      int maxLeftIndex = 0;
      double maxLeft = Double.NEGATIVE_INFINITY;
      int maxRightIndex = 0;
      double maxRight = Double.NEGATIVE_INFINITY;
      for (int i = 1; i < leftMags.length; i++) {
        if (bins[i] < 20 || bins[i] > 20000) {
          continue;
        }
        if (leftMags[i] > maxLeft) {
          maxLeftIndex = i;
          maxLeft = leftMags[i];
        }
        if (rightMags[i] > maxRight) {
          maxRightIndex = i;
          maxRight = rightMags[i];
        }
      }

      notifyListeners(bins[maxLeftIndex], bins[maxRightIndex]);
    }
  }

  private double[] decode(final byte[] buf, boolean decodeLeft) {
    final double[] fbuf = new double[(buf.length / 2) / frameSize];
    int byteNum = 0;
    int i = 0;
    for (int pos = 0; pos < buf.length; pos += frameSize) {
      int sample = byteToIntLittleEndian(buf, pos, frameSize);
      // normalize to [0,1] (not strictly necessary, but makes things easier)
      double normalSample =  sample / NORMALIZATION_FACTOR_2_BYTES;
      if (decodeLeft) {
        if (byteNum < 2) {
          fbuf[i++] = normalSample;
        }
      } else if (byteNum >= 2) {
        fbuf[i++] = normalSample;
      }
      byteNum++;
      byteNum %= 4;
    }
    return fbuf;
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
