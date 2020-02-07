package shapes;

public abstract class Shape {
  protected double weight;
  protected double length;

  public abstract float nextX(double drawingProgress);
  public abstract float nextY(double drawingProgress);
  public abstract Shape rotate(double theta);
  public abstract Shape scale(double factor);
  public abstract Shape translate(Vector vector);

  public double getWeight() {
    return weight;
  }

  public double getLength() {
    return length;
  }
}
