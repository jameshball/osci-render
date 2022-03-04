package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

import static sh.ball.math.Math.round;

public class BitCrushEffect implements SettableEffect {

  private double crush = 2.0;

  public BitCrushEffect() {}

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    double x = vector.getX();
    double y = vector.getY();
    return new Vector2(round(x, crush), round(y, crush));
  }

  @Override
  public void setValue(double value) {
    this.crush = 3.0 * value;
  }
}
