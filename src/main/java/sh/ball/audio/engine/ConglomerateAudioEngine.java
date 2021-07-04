package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.Callable;

public class ConglomerateAudioEngine implements AudioEngine {

  private final XtAudioEngine xtEngine = new XtAudioEngine();
  private final JavaAudioEngine javaEngine = new JavaAudioEngine();

  // TODO: Try and make non-static
  private static List<AudioDevice> xtDevices;
  private static AudioDevice javaDevice;

  private boolean playing = false;

  @Override
  public boolean isPlaying() {
    return playing;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    playing = true;
    if (xtDevices.contains(device)) {
      xtEngine.play(channelGenerator, device);
    } else {
      javaEngine.play(channelGenerator, javaDevice);
    }
    playing = false;
  }

  @Override
  public void stop() {
    xtEngine.stop();
    javaEngine.stop();
  }

  @Override
  public List<AudioDevice> devices() {
    if (xtDevices == null) {
      xtDevices = xtEngine.devices();
    }
    if (javaDevice == null) {
      javaDevice = javaEngine.getDefaultDevice();
    }

    List<AudioDevice> devices = new ArrayList<>(xtDevices);
    devices.add(javaDevice);

    return devices;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return javaEngine.getDefaultDevice();
  }
}
