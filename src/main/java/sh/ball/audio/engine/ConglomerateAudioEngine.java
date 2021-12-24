package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.Callable;

// combines all types of AudioEngines into a single AudioEngine to allow for
// maximum compatibility and support for all audio drivers that have been
// implemented
public class ConglomerateAudioEngine implements AudioEngine {

  // used to determine which OS we are on
  private static final String OS = System.getProperty("os.name", "generic").toLowerCase(Locale.ENGLISH);
  private static final boolean MAC_OS = OS.contains("mac") || OS.contains("darwin");

  private final XtAudioEngine xtEngine = new XtAudioEngine();
  private final JavaAudioEngine javaEngine = new JavaAudioEngine();

  // TODO: Try and make non-static
  private static List<AudioDevice> xtDevices;
  private static List<AudioDevice> javaDevices;

  private boolean playing = false;
  private AudioDevice device;

  @Override
  public boolean isPlaying() {
    return playing;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    playing = true;
    this.device = device;
    if (xtDevices.contains(device)) {
      xtEngine.play(channelGenerator, device);
    } else {
      javaEngine.play(channelGenerator, device);
    }
    playing = false;
    this.device = null;
  }

  @Override
  public void stop() {
    xtEngine.stop();
    javaEngine.stop();
  }

  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    if (javaDevices == null) {
      javaDevices = javaEngine.devices();
    }

    if (xtDevices == null) {
      // XtAudio does not support MacOS so we should ignore all devices on XtAudio if on a mac
      if (MAC_OS) {
        xtDevices = new ArrayList<>();
      } else {
        xtDevices = xtEngine.devices();
      }
    }

    devices.addAll(javaDevices);
    devices.addAll(xtDevices);

    return devices;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return javaEngine.getDefaultDevice();
  }

  @Override
  public AudioDevice currentDevice() {
    return device;
  }
}
