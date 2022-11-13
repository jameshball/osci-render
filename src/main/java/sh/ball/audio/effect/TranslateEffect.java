package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

// Translates the given vector in a sinusoidal fashion if ellipse is true,
// otherwise applies a constant translation
public class TranslateEffect extends PhaseEffect implements SettableEffect {

  private Vector2 translation;
  private boolean ellipse = false;
  private double scale = 1;

  public TranslateEffect(int sampleRate, double speed, Vector2 translation) {
    super(sampleRate, speed);
    this.translation = translation;
  }

  public TranslateEffect(int sampleRate) {
    this(sampleRate, 0, new Vector2());
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (ellipse) {
      double theta = nextTheta();
      return new Vector2(vector.x + translation.x * Math.sin(theta) * scale, vector.y + translation.y * Math.cos(theta) * scale);
    } else {
      return new Vector2(vector.x + translation.x * scale, vector.y + translation.y * scale);
    }
  }

  public void setTranslation(Vector2 translation) {
    this.translation = translation;
  }

  public void setEllipse(boolean ellipse) {
    if (!this.ellipse && ellipse) {
      resetTheta();
    }
    this.ellipse = ellipse;
  }

  public void setScale(double scale) {
    this.scale = scale;
  }

  @Override
  public void setValue(double value) {
    setScale(value);
  }
}
