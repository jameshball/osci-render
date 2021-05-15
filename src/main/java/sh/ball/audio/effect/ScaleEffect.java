package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class ScaleEffect implements Effect {

  private double scale;

  public ScaleEffect(double scale) {
    this.scale = scale;
  }

  public ScaleEffect() {
    this(1);
  }

  public void setScale(double scale) {
    this.scale = scale;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    return vector.scale(scale);
  }
}
