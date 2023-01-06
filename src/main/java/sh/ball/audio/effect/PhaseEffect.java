package sh.ball.audio.effect;

public abstract class PhaseEffect implements Effect {

  protected int sampleRate;

  protected double speed;
  private double phase = 0;

  protected PhaseEffect(int sampleRate, double speed) {
    this.sampleRate = sampleRate;
    this.speed = speed;
  }

  public void resetTheta() {
    phase = 0;
  }

  protected double nextTheta() {
    phase += speed / sampleRate;

    if (phase > 1) {
      phase -= 1;
    }

    return phase * 2 * Math.PI;
  }

  public void setSpeed(double speed) {
    this.speed = speed;
  }

  public void setSampleRate(int sampleRate) {
    this.sampleRate = sampleRate;
  }
}
