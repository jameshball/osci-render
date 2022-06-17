package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

// rotates the vector about (0,0)
public class RotateEffect extends PhaseEffect implements SettableEffect {

  public RotateEffect(int sampleRate, double speed) {
    super(sampleRate, speed);
  }

  public RotateEffect(int sampleRate) {
    this(sampleRate, 0);
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    return vector.rotate(nextTheta());
  }

  @Override
  public void setValue(double value) {
    setSpeed(value);
  }
}
