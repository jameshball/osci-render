package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class RotateEffect extends PhaseEffect {

  public RotateEffect(int sampleRate, double speed) {
    super(sampleRate, speed);
  }

  public RotateEffect(int sampleRate) {
    this(sampleRate, 0);
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (speed != 0) {
      return vector.rotate(nextTheta());
    }

    return vector;
  }
}
