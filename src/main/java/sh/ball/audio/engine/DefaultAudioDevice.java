package sh.ball.audio.engine;

public class DefaultAudioDevice implements AudioDevice {

  final String name;
  final int sampleRate;
  final AudioSample sample;

  public DefaultAudioDevice(String name, int sampleRate, AudioSample sample) {
    this.name = name;
    this.sampleRate = sampleRate;
    this.sample = sample;
  }

  @Override
  public String name() {
    return name;
  }

  @Override
  public int sampleRate() {
    return sampleRate;
  }

  @Override
  public AudioSample sample() {
    return sample;
  }
}
