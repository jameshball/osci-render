package shapes;

public abstract class Shape {
  public static final int DEFAULT_WEIGHT = 1;

  protected double weight;
  protected double length;

  public abstract float nextX(double drawingProgress);
  public abstract float nextY(double drawingProgress);
  public abstract Shape rotate(double theta);
  public abstract Shape scale(double factor);
  public abstract Shape translate(Vector2 vector);
  public abstract Shape setWeight(double weight);

  public double getWeight() {
    return weight;
  }

  public double getLength() {
    return length;
  }
}
