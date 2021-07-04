package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.List;
import java.util.concurrent.Callable;

public class JavaAudioEngine implements AudioEngine {

  @Override
  public boolean isPlaying() {
    return false;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {

  }

  @Override
  public void stop() {

  }

  @Override
  public List<AudioDevice> devices() {
    return null;
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return null;
  }
}
