package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class BulgeEffect implements SettableEffect {

  private double bulge = 0.1;

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    double translatedBulge = -bulge + 1;

    double r = vector.magnitude();
    double theta = Math.atan2(vector.y, vector.x);
    double rn = Math.pow(r, translatedBulge);

    return new Vector2(rn * Math.cos(theta), rn * Math.sin(theta));
  }

  @Override
  public void setValue(double value) {
    this.bulge = value;
  }
}
