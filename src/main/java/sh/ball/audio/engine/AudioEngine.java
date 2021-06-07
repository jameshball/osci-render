package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.concurrent.Callable;
import java.util.concurrent.locks.ReentrantLock;

public interface AudioEngine {
  void play(Callable<Vector2> channelGenerator, ReentrantLock renderLock);
  void stop();
  int sampleRate();
}
