package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

import static sh.ball.math.Math.round;

public class EffectFactory {
  public static Effect vectorCancelling(int frequency) {
    return (count, v) -> count % frequency == 0 ? v.scale(-1) : v;
  }

  public static Effect bitCrush(double value) {
    return (count, v) -> {
      double x = v.getX();
      double y = v.getY();
      return new Vector2(round(x, value), round(y, value));
    };
  }

  public static Effect horizontalDistort(double value) {
    return (count, v) -> {
      if (count % 2 == 0) {
        return v.translate(new Vector2(value, 0));
      } else {
        return v.translate(new Vector2(-value, 0));
      }
    };
  }

  public static Effect verticalDistort(double value) {
    return (count, v) -> {
      if (count % 2 == 0) {
        return v.translate(new Vector2(0, value));
      } else {
        return v.translate(new Vector2(0, -value));
      }
    };
  }
}
