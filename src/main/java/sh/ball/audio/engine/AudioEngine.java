package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.locks.ReentrantLock;

public interface AudioEngine {
  void play(Callable<Vector2> channelGenerator, ReentrantLock renderLock, AudioDevice device);

  void stop();

  List<AudioDevice> devices();

  AudioDevice getDefaultDevice();
}
