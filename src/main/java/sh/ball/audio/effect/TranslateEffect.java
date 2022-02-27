package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

// Translates the given vector in a sinusoidal fashion if ellipse is true,
// otherwise applies a constant translation
public class TranslateEffect extends PhaseEffect {

  private Vector2 translation;
  private boolean ellipse = true;

  public TranslateEffect(int sampleRate, double speed, Vector2 translation) {
    super(sampleRate, speed);
    this.translation = translation;
  }

  public TranslateEffect(int sampleRate) {
    this(sampleRate, 0, new Vector2());
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (!translation.equals(new Vector2())) {
      if (ellipse) {
        double theta = nextTheta();
        return vector.translate(translation.scale(new Vector2(Math.sin(theta), Math.cos(theta))));
      } else {
        return vector.translate(translation);
      }
    }

    return vector;
  }

  public void setTranslation(Vector2 translation) {
    this.translation = translation;
  }

  public void setEllipse(boolean ellipse) {
    this.ellipse = ellipse;
  }
}
