package sh.ball.audio.effect;

public abstract class PhaseEffect implements Effect {

  protected final int sampleRate;

  protected double speed;
  private double phase;

  protected PhaseEffect(int sampleRate, double speed) {
    this.sampleRate = sampleRate;
    this.speed = speed;
  }

  protected double nextTheta() {
    phase += speed / sampleRate;

    if (phase >= 1.0) {
      phase = -1.0;
    }

    return phase * Math.PI;
  }

  public void setSpeed(double speed) {
    this.speed = speed;
  }
}
