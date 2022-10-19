package sh.ball.audio.engine;

import java.util.List;

public interface AudioInput {
  void addListener(AudioInputListener listener);

  void listen(AudioDevice device);

  void stop();

  boolean isAvailable();

  List<AudioDevice> devices();

  AudioDevice getDefaultDevice();
}
