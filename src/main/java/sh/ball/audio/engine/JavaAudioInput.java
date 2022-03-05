package sh.ball.audio.engine;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.List;

// really rough microphone listener - not intended for actual playback of audio
// but more for using the microphone as a signal.
public class JavaAudioInput implements AudioInput {

  private static final int DEFAULT_SAMPLE_RATE = 44100;
  private static final int BUFFER_SIZE = 50;
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
      microphone.open(format, BUFFER_SIZE);

      // I am well aware this is inefficient - sufficient for the needs of
      // modifying sliders
      byte[] data = new byte[2];
      microphone.start();
      while (!stopped) {
        microphone.read(data, 0, 2);
        short sample = (short) ((data[1] << 8) + data[0]);
        for (AudioInputListener listener : listeners) {
          listener.transmit((double) sample / Short.MAX_VALUE);
        }
      }
      microphone.close();
    } catch (LineUnavailableException e) {
      e.printStackTrace();
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
