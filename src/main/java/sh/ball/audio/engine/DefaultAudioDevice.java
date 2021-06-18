package sh.ball.audio.engine;

import java.util.Objects;

public class DefaultAudioDevice implements AudioDevice {

  final String id;
  final String name;
  final int sampleRate;
  final AudioSample sample;

  public DefaultAudioDevice(String id, String name, int sampleRate, AudioSample sample) {
    this.id = id;
    this.name = name;
    this.sampleRate = sampleRate;
    this.sample = sample;
  }

  @Override
  public String id() {
    return id;
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

  @Override
  public String toString() {
    String simplifiedName = name.replaceFirst(" \\(Shared\\)", "");
    simplifiedName = simplifiedName.replaceFirst(" \\(NVIDIA High Definition Audio\\)", "");
    return simplifiedName + " @ " + sampleRate + "kHz, " + sample;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    DefaultAudioDevice that = (DefaultAudioDevice) o;
    return sampleRate == that.sampleRate && Objects.equals(id, that.id) && Objects.equals(name, that.name) && sample == that.sample;
  }

  @Override
  public int hashCode() {
    return Objects.hash(id, name, sampleRate, sample);
  }
}
