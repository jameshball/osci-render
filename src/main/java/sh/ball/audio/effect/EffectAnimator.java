package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class EffectAnimator extends PhaseEffect implements SettableEffect {

  private static final double SPEED_SCALE = 20.0;

  private final SettableEffect effect;

  private AnimationType type = AnimationType.STATIC;
  private double value = 0.5;
  private double actualValue = 0.5;
  private boolean linearDirection = true;

  public EffectAnimator(int sampleRate, SettableEffect effect) {
    super(sampleRate, 1.0);
    this.effect = effect;
  }

  public void setAnimation(AnimationType type) {
    this.type = type;
    this.linearDirection = true;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    switch (type) {
      case STATIC -> actualValue = value;
      case SEESAW -> {
        double scalar = 10 * Math.max(Math.min(actualValue, 1 - actualValue), 0.01);
        double change = scalar * SPEED_SCALE * value / sampleRate;
        if (actualValue + change > 1 || actualValue - change < 0) {
          linearDirection = !linearDirection;
        }
        if (linearDirection) {
          actualValue += change;
        } else {
          actualValue -= change;
        }
      }
      case LINEAR -> {
        double change = SPEED_SCALE * value / sampleRate;
        if (actualValue + change > 1 || actualValue - change < 0) {
          linearDirection = !linearDirection;
        }
        if (linearDirection) {
          actualValue += change;
        } else {
          actualValue -= change;
        }
      }
      case FORWARD -> {
        actualValue += 0.5 * SPEED_SCALE * value / sampleRate;
        if (actualValue > 1) {
          actualValue = 0;
        }
      }
      case REVERSE -> {
        actualValue -= 0.5 * SPEED_SCALE * value / sampleRate;
        if (actualValue < 0) {
          actualValue = 1;
        }
      }
    }
    effect.setValue(actualValue);
    return effect.apply(count, vector);
  }

  @Override
  public void setValue(double value) {
    this.value = value;
    effect.setValue(value);
  }
}
