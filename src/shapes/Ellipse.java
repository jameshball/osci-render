package shapes;

public class Ellipse implements Shape {
  private final double a;
  private final double b;
  private final double weight;
  private final double length;
  private final Vector position;

  public Ellipse(double a, double b, double weight, Vector position) {
    this.a = a;
    this.b = b;
    this.weight = weight;
    this.position = position;
    // Approximation of length.
    this.length = 2 * Math.PI * Math.sqrt((this.a * this.a + this.b * this.b) / 2);
  }

  public Ellipse(double a, double b, Vector position) {
    this(a, b, 100, position);
  }

  public Ellipse(double a, double b) {
    this(a, b, 100, new Vector());
  }

  @Override
  public float nextX(double drawingProgress) {
    return (float) (position.getX() + a * Math.sin(2 * Math.PI * drawingProgress));
  }

  @Override
  public float nextY(double drawingProgress) {
    return (float) (position.getY() + b * Math.cos(2 * Math.PI * drawingProgress));
  }

  // TODO: Implement ellipse rotation.
  @Override
  public Shape rotate(double theta) {
    return this;
  }

  @Override
  public Shape scale(double factor) {
    return new Ellipse(a * factor, b * factor, weight, position.scale(factor));
  }

  // TODO: Implement ellipse translation.
  @Override
  public Shape translate(Vector vector) {
    return new Ellipse(a, b, weight, position.add(vector));
  }

  @Override
  public double getWeight() {
    return weight;
  }

  @Override
  public double getLength() {
    return length;
  }
}
