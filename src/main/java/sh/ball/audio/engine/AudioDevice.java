package sh.ball.audio.engine;

import javax.sound.sampled.Mixer;

public interface AudioDevice {
  String id();

  String name();

  int sampleRate();

  AudioSample sample();

  int channels();

  Mixer.Info mixerInfo();

  boolean output();
}
