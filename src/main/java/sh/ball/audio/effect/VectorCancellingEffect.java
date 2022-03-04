package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class VectorCancellingEffect implements SettableEffect {

  private double frequency = 1;
  private double nextInvert;
  private int lastCount = 0;

  public VectorCancellingEffect() {
  }

  @Override
  public void setValue(double value) {
    this.frequency = 1.0 + 9.0 * value;
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
