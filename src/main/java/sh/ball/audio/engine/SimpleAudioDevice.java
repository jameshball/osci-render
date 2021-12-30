package sh.ball.audio.engine;

import java.util.Objects;

public record SimpleAudioDevice(String id, String name, int sampleRate, AudioSample sample) implements AudioDevice {

  @Override
  public String toString() {
    String simplifiedName = name.replaceFirst(" \\(Shared\\)", "");
    simplifiedName = simplifiedName.replaceFirst(" \\(NVIDIA High Definition Audio\\)", "");
    return simplifiedName + " @ " + sampleRate + "Hz, " + sample;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    SimpleAudioDevice that = (SimpleAudioDevice) o;
    return sampleRate == that.sampleRate && Objects.equals(id, that.id) && Objects.equals(name, that.name) && sample == that.sample;
  }

  @Override
  public int hashCode() {
    return Objects.hash(id, name, sampleRate, sample);
  }
}
