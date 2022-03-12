package sh.ball.math;

public class Math {

  public static double EPSILON = 0.0001;

  public static double round(double value, double places) {
    long factor = (long) java.lang.Math.pow(10, places);
    value = value * factor;
    long tmp = java.lang.Math.round(value);
    return (double) tmp / factor;
  }

  public static boolean parseable(String value) {
    try {
      Double.parseDouble(value);
    } catch (NumberFormatException e) {
      return false;
    }
    return true;
  }

  public static double tryParse(String value) {
    try {
      return Double.parseDouble(value);
    } catch (NumberFormatException e) {
      return 0;
    }
  }
}
