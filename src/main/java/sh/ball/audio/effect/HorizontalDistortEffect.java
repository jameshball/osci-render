package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class HorizontalDistortEffect implements SettableEffect {

  private double value;

  public HorizontalDistortEffect(double value) {
    this.value = value;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (count % 2 == 0) {
      return vector.translate(new Vector2(value, 0));
    } else {
      return vector.translate(new Vector2(-value, 0));
    }
  }

  @Override
  public void setValue(double value) {
    this.value = value;
  }
}
