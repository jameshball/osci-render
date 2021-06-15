package sh.ball.audio.engine;

public interface AudioDevice {
  String name();

  int sampleRate();

  AudioSample sample();
}
