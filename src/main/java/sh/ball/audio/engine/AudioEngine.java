package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.List;
import java.util.concurrent.Callable;

public interface AudioEngine {
  boolean isPlaying();

  void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception;

  void stop();

  List<AudioDevice> devices();

  AudioDevice getDefaultOutputDevice();
  AudioDevice getDefaultInputDevice();

  AudioDevice currentOutputDevice();
  AudioDevice currentInputDevice();

  void setBrightness(double brightness);

  void addListener(AudioInputListener listener);

  void listen(AudioDevice device);

  boolean inputAvailable();
}
