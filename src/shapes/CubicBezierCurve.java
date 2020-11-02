package shapes;

public class CubicBezierCurve extends Shape {

  private final Vector2 p0;
  private final Vector2 p1;
  private final Vector2 p2;
  private final Vector2 p3;

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, double weight) {
    this.p0 = p0;
    this.p1 = p1;
    this.p2 = p2;
    this.p3 = p3;
    this.weight = weight;
    this.length = new Line(p0, p3).length;
  }

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3) {
    this(p0, p1, p2, p3, DEFAULT_WEIGHT);
  }

  @Override
  public Vector2 nextVector(double t) {
    return p0.scale(Math.pow(1 - t, 3))
        .add(p1.scale(3 * Math.pow(1 - t, 2) * t))
        .add(p2.scale(3 * (1 - t) * Math.pow(t, 2)))
        .add(p3.scale(Math.pow(t, 3)));
  }

  @Override
  public CubicBezierCurve rotate(double theta) {
    return new CubicBezierCurve(p0.rotate(theta), p1.rotate(theta), p2.rotate(theta),
        p3.rotate(theta), weight);
  }

  @Override
  public CubicBezierCurve scale(double factor) {
    return scale(new Vector2(factor));
  }

  @Override
  public CubicBezierCurve scale(Vector2 vector) {
    return new CubicBezierCurve(p0.scale(vector), p1.scale(vector), p2.scale(vector),
        p3.scale(vector), weight);
  }

  @Override
  public CubicBezierCurve translate(Vector2 vector) {
    return new CubicBezierCurve(p0.translate(vector), p1.translate(vector), p2.translate(vector),
        p3.translate(vector), weight);
  }

  @Override
  public CubicBezierCurve setWeight(double weight) {
    return new CubicBezierCurve(p0, p1, p2, p3, weight);
  }
}
