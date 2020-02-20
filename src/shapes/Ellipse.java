package shapes;

public final class Ellipse extends Shape {
  private final double a;
  private final double b;
  private final double rotation;
  private final Vector2 position;

  public Ellipse(double a, double b, double weight, double rotation, Vector2 position) {
    this.a = a;
    this.b = b;
    this.weight = weight;
    this.rotation = rotation;
    this.position = position;
    // Approximation of length.
    this.length = 2 * Math.PI * Math.sqrt((a * a + b * b) / 2);
  }

  public Ellipse(double a, double b, Vector2 position) {
    this(a, b, Shape.DEFAULT_WEIGHT, 0, position);
  }

  public Ellipse(double a, double b) {
    this(a, b, Shape.DEFAULT_WEIGHT, 0, new Vector2());
  }

  @Override
  public float nextX(double drawingProgress) {
    return (float) (position.getX()
      + a * Math.cos(2 * Math.PI * drawingProgress) * Math.cos(rotation)
      - b * Math.sin(2 * Math.PI * drawingProgress) * Math.sin(rotation));
  }

  @Override
  public float nextY(double drawingProgress) {
    return (float) (position.getY()
      + a * Math.cos(2 * Math.PI * drawingProgress) * Math.sin(rotation)
      + b * Math.sin(2 * Math.PI * drawingProgress) * Math.cos(rotation));
  }

  @Override
  public Ellipse rotate(double theta) {
    if (theta + rotation > 2 * Math.PI) {
      theta -= 2 * Math.PI;
    }

    return new Ellipse(a, b, weight, theta, position);
  }

  @Override
  public Ellipse scale(double factor) {
    return new Ellipse(a * factor, b * factor, weight, rotation, position.scale(factor));
  }

  @Override
  public Ellipse translate(Vector2 vector) {
    return new Ellipse(a, b, weight, rotation, position.translate(vector));
  }

  @Override
  public Ellipse setWeight(double weight) {
    return new Ellipse(a, b, weight, rotation, position);
  }
}
