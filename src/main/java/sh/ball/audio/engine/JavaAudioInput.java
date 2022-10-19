package sh.ball.audio.engine;

import javax.sound.sampled.*;
import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;
import java.util.stream.Stream;

import static sh.ball.gui.Gui.logger;

public class JavaAudioInput implements AudioInput {

  private static final int DEFAULT_SAMPLE_RATE = 192000;
  private static final int DEFAULT_NUM_CHANNELS = 2;
  // java sound doesn't support anything more than 16 bit :(
  private static final int BIT_DEPTH = 16;
  // mono
  private static final int NUM_CHANNELS = 2;
  private static final boolean BIG_ENDIAN = false;
  private static final boolean SIGNED_SAMPLE = true;
  private static final AudioFormat format = new AudioFormat(DEFAULT_SAMPLE_RATE, BIT_DEPTH, NUM_CHANNELS, SIGNED_SAMPLE, BIG_ENDIAN);
  private final List<AudioInputListener> listeners = new ArrayList<>();

  private TargetDataLine microphone;
  private boolean stopped = false;

  @Override
  public void addListener(AudioInputListener listener) {
    listeners.add(listener);
  }

  @Override
  public void listen(AudioDevice device) {
    if (device.output()) {
      throw new RuntimeException("Device is not an input device");
    }

    try {
      AudioFormat format = new AudioFormat((float) device.sampleRate(), BIT_DEPTH, device.channels(), SIGNED_SAMPLE, BIG_ENDIAN);
      if (device.mixerInfo() != null) {
        this.microphone = AudioSystem.getTargetDataLine(format, device.mixerInfo());
      } else {
        this.microphone = AudioSystem.getTargetDataLine(format);
      }

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
    return devices().size() > 0;
  }

  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    Line.Info audioInput = new Line.Info(TargetDataLine.class);
    List<Mixer.Info> inputDeviceInfos = new ArrayList<>();

    Mixer.Info[] infos = AudioSystem.getMixerInfo();
    for (Mixer.Info info : infos) {
      Mixer mixer = AudioSystem.getMixer(info);
      if (mixer.isLineSupported(audioInput)) {
        inputDeviceInfos.add(info);
      }
    }

    for (Mixer.Info info : inputDeviceInfos) {
      Stream.of(192000, 96000, 48000, 44100).forEach(rate ->
        Stream.of(1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12).forEach(channels -> {
          try {
            AudioFormat format = new AudioFormat((float) rate, BIT_DEPTH, channels, SIGNED_SAMPLE, BIG_ENDIAN);
            TargetDataLine line = AudioSystem.getTargetDataLine(format, info);
            devices.add(new SimpleAudioDevice(info.getName() + "-" + rate, info.getName(), rate, AudioSample.INT16, channels, info, false));
          } catch (Exception ignored) {}
        })
      );
    }

    return devices;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return new SimpleAudioDevice("default", "default", DEFAULT_SAMPLE_RATE, AudioSample.INT16, DEFAULT_NUM_CHANNELS, null, false);
  }
}
