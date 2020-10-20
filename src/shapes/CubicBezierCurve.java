package shapes;

public class CubicBezierCurve extends Shape {

  private final Vector2 p0;
  private final Vector2 p1;
  private final Vector2 p2;
  private final Vector2 p3;
  private final double factor;
  private final Vector2 translation;

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, double weight,
      double factor, Vector2 translation) {
    this.p0 = p0;
    this.p1 = p1;
    this.p2 = p2;
    this.p3 = p3;
    this.weight = weight;
    this.factor = factor;
    this.translation = translation;
    Line temp = new Line(p0, p3);
    this.length = temp.length;
  }

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, double factor,
      Vector2 translation) {
    this(p0, p1, p2, p3, DEFAULT_WEIGHT, factor, translation);
  }

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3, double factor) {
    this(p0, p1, p2, p3, DEFAULT_WEIGHT, factor, new Vector2());
  }

  public CubicBezierCurve(Vector2 p0, Vector2 p1, Vector2 p2, Vector2 p3) {
    this(p0, p1, p2, p3, DEFAULT_WEIGHT, 1, new Vector2());
  }

  @Override
  public float nextX(double t) {
    return (float) (Math.pow(1 - t, 3) * factor * p0.getX()
        + 3 * Math.pow(1 - t, 2) * t * factor * p1.getX()
        + 3 * (1 - t) * Math.pow(t, 2) * factor * p2.getX()
        + Math.pow(t, 3) * factor * p3.getX());
  }

  @Override
  public float nextY(double t) {
    return (float) (Math.pow(1 - t, 3) * factor * p0.getY()
        + 3 * Math.pow(1 - t, 2) * t * factor * p1.getY()
        + 3 * (1 - t) * Math.pow(t, 2) * factor * p2.getY()
        + Math.pow(t, 3) * factor * p3.getY());
  }

  @Override
  public Shape rotate(double theta) {
    return this;
  }

  @Override
  public Shape scale(double factor) {
    return new CubicBezierCurve(p0, p1, p2, p3, weight, factor, translation);
  }

  @Override
  public Shape translate(Vector2 vector) {
    return new CubicBezierCurve(p0, p1, p2, p3, weight, factor, translation.translate(vector));
  }

  @Override
  public Shape setWeight(double weight) {
    return new CubicBezierCurve(p0, p1, p2, p3, weight, factor, translation);
  }
}
