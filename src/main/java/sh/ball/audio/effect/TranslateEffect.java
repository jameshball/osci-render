package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

// Translates the given vector in a sinusoidal fashion
public class TranslateEffect extends PhaseEffect {

  private Vector2 translation;

  public TranslateEffect(int sampleRate, double speed, Vector2 translation) {
    super(sampleRate, speed);
    this.translation = translation;
  }

  public TranslateEffect(int sampleRate) {
    this(sampleRate, 0, new Vector2());
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (speed != 0 && !translation.equals(new Vector2())) {
      return vector.translate(translation.scale(Math.sin(nextTheta())));
    }

    return vector;
  }

  public void setTranslation(Vector2 translation) {
    this.translation = translation;
  }
}
