package sh.ball.audio.engine;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

// really rough microphone listener - not intended for actual playback of audio
// but more for using the microphone as a signal.
public class JavaAudioInput implements AudioInput {

  private static final int DEFAULT_SAMPLE_RATE = 44100;
  private static final int CHUNK_SIZE = 512;
  private static final int STEP_SIZE = 32;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  // mono
  private static final int NUM_CHANNELS = 1;
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

      byte[] data = new byte[CHUNK_SIZE];
      microphone.start();
      while (!stopped) {
        if (microphone.available() >= CHUNK_SIZE) {
          microphone.read(data, 0, CHUNK_SIZE);
          for (int i = 0; i < CHUNK_SIZE / 2; i += 2 * STEP_SIZE) {
            short sample = (short) ((data[2 * i + 1] << 8) + data[2 * i]);
            for (AudioInputListener listener : listeners) {
              listener.transmit((double) sample / Short.MAX_VALUE);
            }
          }
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
