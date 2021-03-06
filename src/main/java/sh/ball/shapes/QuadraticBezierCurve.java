package sh.ball.shapes;

public class QuadraticBezierCurve extends Shape {

  private final Vector2 p0;
  private final Vector2 p1;
  private final Vector2 p2;

  public QuadraticBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2) {
    this.p0 = p0;
    this.p1 = p1;
    this.p2 = p2;
    this.length = new Line(p0, p2).length;
  }

  @Override
  public Vector2 nextVector(double t) {
    return p1.add(p0.sub(p1).scale(Math.pow(1 - t, 2)))
      .add(p2.sub(p1).scale(Math.pow(t, 2)));
  }

  @Override
  public QuadraticBezierCurve rotate(double theta) {
    return new QuadraticBezierCurve(p0.rotate(theta), p1.rotate(theta), p2.rotate(theta));
  }

  @Override
  public QuadraticBezierCurve scale(double factor) {
    return new QuadraticBezierCurve(p0.scale(factor), p1.scale(factor), p2.scale(factor));
  }

  @Override
  public QuadraticBezierCurve scale(Vector2 vector) {
    return new QuadraticBezierCurve(p0.scale(vector), p1.scale(vector), p2.scale(vector));
  }

  @Override
  public QuadraticBezierCurve translate(Vector2 vector) {
    return new QuadraticBezierCurve(p0.translate(vector), p1.translate(vector),
      p2.translate(vector));
  }
}
