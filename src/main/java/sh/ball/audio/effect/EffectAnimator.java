package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class EffectAnimator extends PhaseEffect implements SettableEffect {

  public static final int DEFAULT_SAMPLE_RATE = 192000;

  private static final double SPEED_SCALE = 20.0;

  private final SettableEffect effect;

  private AnimationType type = AnimationType.STATIC;
  private boolean justSetToStatic = true;
  private double targetValue = 0.5;
  private double actualValue = 0.5;
  private boolean linearDirection = true;
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
    this.linearDirection = true;
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
    double minValue = min;
    double maxValue = max;
    double range = maxValue - minValue;
    if (range <= 0) {
      return vector;
    }
    double normalisedTargetValue = (targetValue - minValue) / range;
    double normalisedActualValue = (actualValue - minValue) / range;
    switch (type) {
      case STATIC -> {
        if (justSetToStatic) {
          actualValue = targetValue;
          justSetToStatic = false;
          effect.setValue(actualValue);
        }
        return effect.apply(count, vector);
      }
      case SEESAW -> {
        double scalar = 10 * Math.max(Math.min(normalisedActualValue, 1 - normalisedActualValue), 0.01);
        double change = range * scalar * SPEED_SCALE * normalisedTargetValue / sampleRate;
        if (actualValue + change > maxValue || actualValue - change < minValue) {
          linearDirection = !linearDirection;
        }
        if (linearDirection) {
          actualValue += change;
        } else {
          actualValue -= change;
        }
      }
      case LINEAR -> {
        double change = range * SPEED_SCALE * normalisedTargetValue / sampleRate;
        if (actualValue + change > maxValue || actualValue - change < minValue) {
          linearDirection = !linearDirection;
        }
        if (linearDirection) {
          actualValue += change;
        } else {
          actualValue -= change;
        }
      }
      case FORWARD -> {
        actualValue += range * 0.5 * SPEED_SCALE * normalisedTargetValue / sampleRate;
        if (actualValue > maxValue) {
          actualValue = minValue;
        }
      }
      case REVERSE -> {
        actualValue -= range * 0.5 * SPEED_SCALE * normalisedTargetValue / sampleRate;
        if (actualValue < minValue) {
          actualValue = maxValue;
        }
      }
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
