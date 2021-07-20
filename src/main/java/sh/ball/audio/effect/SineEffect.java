package sh.ball.audio.effect;

import sh.ball.audio.FrequencyListener;
import sh.ball.shapes.Vector2;

public class SineEffect extends PhaseEffect {

  private static final double DEFAULT_VOLUME = 1;

  private double frequency;
  private double volume;

  public SineEffect(int sampleRate, double frequency, double volume) {
    super(sampleRate, 2);
    this.frequency = frequency;
    this.volume = Math.max(Math.min(volume, 1), 0);
  }

  public SineEffect(int sampleRate, double frequency) {
    this(sampleRate, frequency, DEFAULT_VOLUME);
  }

  public void setVolume(double volume) {
    this.volume = volume;
  }

  public void setFrequency(double frequency) {
    this.frequency = frequency;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    double theta = nextTheta();
    double x = vector.getX() + volume * Math.sin(frequency * theta);
    double y = vector.getY() + volume * Math.cos(frequency * theta);

    return new Vector2(x, y);
  }
}
