package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

public class EventFactory {
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

  private static double round(double value, double places) {
    if (places < 0) throw new IllegalArgumentException();

    long factor = (long) Math.pow(10, places);
    value = value * factor;
    long tmp = Math.round(value);
    return (double) tmp / factor;
  }
}
