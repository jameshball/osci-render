package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class VerticalDistortEffect implements SettableEffect {

  private double value;

  public VerticalDistortEffect(double value) {
    this.value = value;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (count % 2 == 0) {
      return vector.translate(new Vector2(0, value));
    } else {
      return vector.translate(new Vector2(0, -value));
    }
  }

  @Override
  public void setValue(double value) {
    this.value = value;
  }
}
