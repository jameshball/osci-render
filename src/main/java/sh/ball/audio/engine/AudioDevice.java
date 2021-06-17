package sh.ball.audio.engine;

public interface AudioDevice {
  String id();

  String name();

  int sampleRate();

  AudioSample sample();
}
