package sh.ball.shapes;

public class QuadraticBezierCurve extends CubicBezierCurve {

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2) {
    super(p0, p0.add(p1.sub(p0).scale(2.0/3.0)), p2.add(p1.sub(p2).scale(2.0/3.0)), p2);
  }
}
