package shapes;

import java.util.ArrayList;
import java.util.List;

public abstract class Shape {

  public static final int DEFAULT_WEIGHT = 80;

  protected double weight = DEFAULT_WEIGHT;
  protected double length;

  public abstract Vector2 nextVector(double drawingProgress);

  public abstract Shape rotate(double theta);

  public abstract Shape scale(double factor);

  public abstract Shape scale(Vector2 vector);

  public abstract Shape translate(Vector2 vector);

  public abstract Shape setWeight(double weight);

  public double getWeight() {
    return weight;
  }

  public double getLength() {
    return length;
  }

  /* SHAPE HELPER FUNCTIONS */

  // Normalises shapes between the coords -1 and 1 for proper scaling on an oscilloscope. May not
  // work perfectly with curves that heavily deviate from their start and end points.
  public static List<List<Shape>> normalize(List<List<Shape>> shapeLists) {
    double maxVertex = 0;

    for (List<Shape> shapes : shapeLists) {
      for (Shape shape : shapes) {
        Vector2 startVector = shape.nextVector(0);
        Vector2 endVector = shape.nextVector(1);

        double maxX = Math.max(Math.abs(startVector.getX()), Math.abs(endVector.getX()));
        double maxY = Math.max(Math.abs(startVector.getY()), Math.abs(endVector.getY()));

        maxVertex = Math.max(Math.max(maxX, maxY), maxVertex);
      }
    }

    double factor = 2 / maxVertex;

    for (List<Shape> shapes : shapeLists) {
      for (int i = 0; i < shapes.size(); i++) {
        shapes.set(i, shapes.get(i)
            .scale(new Vector2(factor, -factor))
            .translate(new Vector2(-1, 1)));
      }
    }

    return shapeLists;
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, Vector2 start,
      double weight) {
    List<Shape> polygon = new ArrayList<>();

    double theta = angleJump * 2 * Math.PI / sides;
    Vector2 rotated = start.rotate(theta);
    polygon.add(new Line(start, rotated, weight));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta), weight));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, Vector2 start) {
    return generatePolygram(sides, angleJump, start, Line.DEFAULT_WEIGHT);
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, double scale,
      double weight) {
    return generatePolygram(sides, angleJump, new Vector2(scale, scale), weight);
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, double scale) {
    return generatePolygram(sides, angleJump, new Vector2(scale, scale));
  }

  public static List<Shape> generatePolygon(int sides, Vector2 start, double weight) {
    return generatePolygram(sides, 1, start, weight);
  }

  public static List<Shape> generatePolygon(int sides, Vector2 start) {
    return generatePolygram(sides, 1, start);
  }

  public static List<Shape> generatePolygon(int sides, double scale, double weight) {
    return generatePolygon(sides, new Vector2(scale, scale), weight);
  }

  public static List<Shape> generatePolygon(int sides, double scale) {
    return generatePolygon(sides, new Vector2(scale, scale));
  }

  public static double totalLength(List<? extends Shape> shapes) {
    return shapes
        .stream()
        .map(Shape::getLength)
        .reduce(Double::sum)
        .orElse(0d);
  }
}
