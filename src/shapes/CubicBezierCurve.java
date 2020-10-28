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
  public float nextX(double t) {
    return (float) (Math.pow(1 - t, 3) * p0.getX()
        + 3 * Math.pow(1 - t, 2) * t * p1.getX()
        + 3 * (1 - t) * Math.pow(t, 2) * p2.getX()
        + Math.pow(t, 3) * p3.getX());
  }

  @Override
  public float nextY(double t) {
    return (float) (Math.pow(1 - t, 3) * p0.getY()
        + 3 * Math.pow(1 - t, 2) * t * p1.getY()
        + 3 * (1 - t) * Math.pow(t, 2) * p2.getY()
        + Math.pow(t, 3) * p3.getY());
  }

  @Override
  public CubicBezierCurve rotate(double theta) {
    return this;
  }

  @Override
  public CubicBezierCurve scale(double factor) {
    return new CubicBezierCurve(p0.scale(factor), p1.scale(factor), p2.scale(factor), p3.scale(factor), weight);
  }

  @Override
  public CubicBezierCurve scale(Vector2 vector) {
    return new CubicBezierCurve(p0.scale(vector), p1.scale(vector), p2.scale(vector), p3.scale(vector), weight);
  }

  @Override
  public CubicBezierCurve translate(Vector2 vector) {
    return new CubicBezierCurve(p0.translate(vector), p1.translate(vector), p2.translate(vector), p3.translate(vector), weight);
  }

  @Override
  public CubicBezierCurve setWeight(double weight) {
    return new CubicBezierCurve(p0, p1, p2, p3, weight);
  }
}
