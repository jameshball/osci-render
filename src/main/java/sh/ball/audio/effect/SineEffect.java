package sh.ball.audio.effect;

import sh.ball.audio.FrequencyListener;
import sh.ball.shapes.Vector2;

public class SineEffect extends PhaseEffect implements FrequencyListener {

  private static final double DEFAULT_VOLUME = 0.2;

  private double frequency;
  private double lastFrequency;
  private double volume;

  public SineEffect(int sampleRate, double volume) {
    super(sampleRate, 2);
    this.volume = Math.max(Math.min(volume, 1), 0);
  }

  public SineEffect(int sampleRate) {
    this(sampleRate, DEFAULT_VOLUME);
  }

  public void update() {
    frequency = lastFrequency;
  }

  public void setVolume(double volume) {
    this.volume = volume;
  }

  @Override
  public void updateFrequency(double leftFrequency, double rightFrequency) {
    lastFrequency = leftFrequency;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    double theta = nextTheta();
    double x = vector.getX() + volume * Math.sin(frequency * theta);
    double y = vector.getY() + volume * Math.sin(frequency * theta);

    return new Vector2(x, y);
  }
}
