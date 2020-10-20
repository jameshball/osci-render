package shapes;

public class QuadraticBezierCurve extends CubicBezierCurve {

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, double weight,
      double factor, Vector2 translation) {
    super(p0, p1, p1, p2, weight, factor, translation);
  }

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, double factor,
      Vector2 translation) {
    super(p0, p1, p1, p2, factor, translation);
  }

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, double factor) {
    super(p0, p1, p1, p2, factor);
  }

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2) {
    super(p0, p1, p1, p2);
  }
}
