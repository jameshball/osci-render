package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class EffectAnimator extends PhaseEffect implements SettableEffect {

  public static final int DEFAULT_SAMPLE_RATE = 192000;

  private final SettableEffect effect;

  private AnimationType type = AnimationType.STATIC;
  private boolean justSetToStatic = true;
  private double targetValue = 0.5;
  private double actualValue = 0.5;
  private double min;
  private double max;

  public EffectAnimator(int sampleRate, SettableEffect effect, double min, double max) {
    super(sampleRate, 1.0);
    this.effect = effect;
    this.min = min;
    this.max = max;
  }

  public EffectAnimator(int sampleRate, SettableEffect effect) {
    this(sampleRate, effect, 0, 1);
  }

  public void setAnimation(AnimationType type) {
    this.type = type;
    if (type == AnimationType.STATIC) {
      justSetToStatic = true;
    }
  }

  public void setMin(double min) {
    this.min = min;
  }

  public void setMax(double max) {
    this.max = max;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (type == AnimationType.STATIC) {
      if (justSetToStatic) {
        actualValue = targetValue;
        justSetToStatic = false;
        effect.setValue(actualValue);
      }
      return effect.apply(count, vector);
    }

    double minValue = min;
    double maxValue = max;
    double phase = nextTheta();
    double percentage = phase / (2 * Math.PI);
    switch (type) {
      case SEESAW -> {
        // modified sigmoid function
        actualValue = (percentage < 0.5) ? percentage * 2 : (1 - percentage) * 2;
        actualValue = 1 / (1 + Math.exp(-16 * (actualValue - 0.5)));
        actualValue = actualValue * (maxValue - minValue) + minValue;
      }
      case SINE -> {
        actualValue = Math.sin(phase) * 0.5 + 0.5;
        actualValue = actualValue * (maxValue - minValue) + minValue;
      }
      case LINEAR -> {
        actualValue = (percentage < 0.5) ? percentage * 2 : (1 - percentage) * 2;
        actualValue = actualValue * (maxValue - minValue) + minValue;
      }
      case SQUARE -> actualValue = (percentage < 0.5) ? maxValue : minValue;
      case FORWARD -> actualValue = percentage * (maxValue - minValue) + minValue;
      case REVERSE -> actualValue = (1 - percentage) * (maxValue - minValue) + minValue;
    }
    if (actualValue > maxValue) {
      actualValue = maxValue;
    } else if (actualValue < minValue) {
      actualValue = minValue;
    }
    effect.setValue(actualValue);
    return effect.apply(count, vector);
  }

  public void updateValue() {
    effect.setValue(targetValue);
  }

  @Override
  public void setValue(double value) {
    this.targetValue = value;
    effect.setValue(value);
  }
}
