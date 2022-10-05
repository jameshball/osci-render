package sh.ball.audio.engine;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

// really rough microphone listener - not intended for actual playback of audio
// but more for using the microphone as a signal.
public class JavaAudioInput implements AudioInput {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  // mono
  private static final int NUM_CHANNELS = 2;
  private static final boolean BIG_ENDIAN = false;
  private static final boolean SIGNED_SAMPLE = true;
  private static final AudioFormat format = new AudioFormat(DEFAULT_SAMPLE_RATE, BIT_DEPTH, NUM_CHANNELS, SIGNED_SAMPLE, BIG_ENDIAN);
  private final List<AudioInputListener> listeners = new ArrayList<>();

  private final TargetDataLine microphone;

  private boolean stopped = false;

  public JavaAudioInput() {
    TargetDataLine mic;
    try {
      DataLine.Info info = new DataLine.Info(TargetDataLine.class, format);
      mic = (TargetDataLine) AudioSystem.getLine(info);
    } catch (LineUnavailableException | IllegalArgumentException e) {
      logger.log(Level.INFO, e.getMessage(), e);
      mic = null;
    }
    microphone = mic;
  }

  @Override
  public void addListener(AudioInputListener listener) {
    listeners.add(listener);
  }

  @Override
  public void run() {
    if (microphone == null) {
      return;
    }
    try {
      microphone.open(format);

      byte[] data = new byte[10000];
      double[] samples;
      microphone.start();
      while (!stopped) {
        microphone.read(data, 0, data.length);
        samples = new double[data.length / 2];

        for (int i = 0; i < data.length; i += 2) {
          samples[i / 2] = (double) ((data[i] & 0xFF) | (data[i + 1] << 8)) / 32768.0;
        }

        for (AudioInputListener listener : listeners) {
          listener.transmit(samples);
        }
      }
      microphone.close();
    } catch (LineUnavailableException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }

  @Override
  public void stop() {
    stopped = true;
  }

  @Override
  public boolean isAvailable() {
    return microphone != null;
  }
}
