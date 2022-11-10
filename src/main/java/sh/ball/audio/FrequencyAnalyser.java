package sh.ball.audio;

import be.tarsos.dsp.pitch.DynamicWavelet;
import be.tarsos.dsp.pitch.PitchDetectionResult;
import be.tarsos.dsp.pitch.PitchDetector;

import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class FrequencyAnalyser<S> implements Runnable {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // increase this for higher frequency resolution, but less frequent frequency calculation
  private static final int DEFAULT_POWER_OF_TWO = 15;

  private final AudioPlayer<S> audioPlayer;
  private final List<FrequencyListener> listeners = new ArrayList<>();
  private final int sampleRate;
  private final int powerOfTwo;

  private volatile boolean stopped;

  public FrequencyAnalyser(AudioPlayer<S> audioPlayer, int sampleRate) {
    this.audioPlayer = audioPlayer;
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
    float[] leftSamples = new float[buf.length / 2];
    float[] rightSamples = new float[buf.length / 2];
    PitchDetector pitchDetector = new DynamicWavelet(sampleRate, buf.length / 2);

    while (!stopped) {
      try {
        audioPlayer.read(buf);
      } catch (InterruptedException e) {
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
      for (int i = 0; i < buf.length; i += 2) {
        leftSamples[i / 2] = (float) buf[i];
        rightSamples[i / 2] = (float) buf[i + 1];
      }

      PitchDetectionResult leftFrequency = pitchDetector.getPitch(leftSamples);
      PitchDetectionResult rightFrequency = pitchDetector.getPitch(rightSamples);

      notifyListeners(leftFrequency.getPitch(), rightFrequency.getPitch());
    }
  }

  public void stop() {
    stopped = true;
  }
}
