package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class VectorCancellingEffect implements Effect {

  private float frequency;
  private float nextInvert;
  private int lastCount = 0;

  public VectorCancellingEffect(float frequency) {
    this.frequency = frequency;
  }

  public void setFrequency(float frequency) {
    this.frequency = frequency;
  }

  @Override
  public Vector2 apply(int count, Vector2 v) {
    if (count < lastCount) {
      nextInvert = nextInvert - lastCount + frequency;
    }
    lastCount = count;
    if (count >= nextInvert) {
      nextInvert += frequency;
      return v;
    } else {
      return v.scale(-1);
    }
  }
}
