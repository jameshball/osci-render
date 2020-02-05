package shapes;

public class Ellipse implements Shape {
  private final double a;
  private final double b;
  private final double weight;
  private final double length;

  public Ellipse(double a, double b, double weight) {
    this.a = a;
    this.b = b;
    this.weight = weight;
    // Approximation of length.
    this.length = 2 * Math.PI * Math.sqrt((this.a * this.a + this.b * this.b) / 2);
  }

  public Ellipse(double a, double b) {
    this(a, b, 100);
  }

  @Override
  public float nextX(double drawingProgress) {
    return (float) (a * Math.sin(2 * Math.PI * drawingProgress));
  }

  @Override
  public float nextY(double drawingProgress) {
    return (float) (b * Math.cos(2 * Math.PI * drawingProgress));
  }

  // TODO: Implement ellipse rotation.
  @Override
  public Shape rotate(double theta) {
    return this;
  }

  @Override
  public Shape scale(double factor) {
    return new Ellipse(a * factor, b * factor, weight);
  }

  // TODO: Implement ellipse translation.
  @Override
  public Shape translate(Vector vector) {
    return this;
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
