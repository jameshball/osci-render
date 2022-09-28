package sh.ball.audio;

import sh.ball.math.fft.FFT;

import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class FrequencyAnalyser<S> implements Runnable {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // increase this for higher frequency resolution, but less frequent frequency calculation
  private static final int DEFAULT_POWER_OF_TWO = 18;

  private final AudioPlayer<S> audioPlayer;
  private final List<FrequencyListener> listeners = new ArrayList<>();
  private final int frameSize;
  private final int sampleRate;
  private final int powerOfTwo;

  private volatile boolean stopped;

  public FrequencyAnalyser(AudioPlayer<S> audioPlayer, int frameSize, int sampleRate) {
    this.audioPlayer = audioPlayer;
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
    double[] buf = new double[2 << (powerOfTwo - 1)];

    while (!stopped) {
      try {
        audioPlayer.read(buf);
      } catch (InterruptedException e) {
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
      double[] leftSamples = new double[buf.length / 2];
      double[] rightSamples = new double[buf.length / 2];
      for (int i = 0; i < buf.length; i += 2) {
        leftSamples[i / 2] = buf[i];
        rightSamples[i / 2] = buf[i + 1];
      }

      FFT leftFft = new FFT(leftSamples, null, false, true);
      FFT rightFft = new FFT(rightSamples, null, false, true);

      double[] leftMags = leftFft.getMagnitudeSpectrum();
      double[] rightMags = rightFft.getMagnitudeSpectrum();
      double[] bins = leftFft.getBinLabels(sampleRate);

      int maxLeftIndex = 0;
      double maxLeft = Double.NEGATIVE_INFINITY;
      int maxRightIndex = 0;
      double maxRight = Double.NEGATIVE_INFINITY;
      for (int i = 0; i < leftMags.length; i++) {
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

  public void stop() {
    stopped = true;
  }
}
