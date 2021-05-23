package sh.ball.audio.effect;

public abstract class PhaseEffect implements Effect {

  private static final double LARGE_VAL = 2 << 20;

  protected final int sampleRate;

  protected double speed;
  private double phase = -LARGE_VAL;

  protected PhaseEffect(int sampleRate, double speed) {
    this.sampleRate = sampleRate;
    this.speed = speed;
  }

  protected double nextTheta() {
    phase += speed / sampleRate;

    if (phase >= LARGE_VAL) {
      phase = -LARGE_VAL;
    }

    return phase * Math.PI;
  }

  public void setSpeed(double speed) {
    this.speed = speed;
  }
}
