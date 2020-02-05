package shapes;

public interface Shape {
  float nextX(double drawingProgress);
  float nextY(double drawingProgress);
  Shape rotate(double theta);
  Shape scale(double factor);
  Shape translate(Vector vector);
  double getWeight();
  double getLength();
}
