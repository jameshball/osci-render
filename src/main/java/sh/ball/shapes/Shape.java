package sh.ball.shapes;

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

  // Normalises sh.ball.shapes between the coords -1 and 1 for proper scaling on an oscilloscope. May not
  // work perfectly with curves that heavily deviate from their start and end points.
  public static List<Shape> normalize(List<Shape> shapes) {
    double maxAbsVertex = maxAbsVector(shapes).getX();
    List<Shape> normalizedShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      normalizedShapes.add(shape.scale(new Vector2(2 / maxAbsVertex, -2 / maxAbsVertex)));
    }

    return center(normalizedShapes);
  }

  public static List<Shape> center(List<Shape> shapes) {
    Vector2 maxVector = maxVector(shapes);
    double height = height(shapes);

    return translate(shapes, new Vector2(-1, -maxVector.getY() + height / 2));
  }

  public static Vector2 maxAbsVector(List<Shape> shapes) {
    double maxX = 0;
    double maxY = 0;

    for (Shape shape : shapes) {
      Vector2 startVector = shape.nextVector(0);
      Vector2 endVector = shape.nextVector(1);

      double x = Math.max(Math.abs(startVector.getX()), Math.abs(endVector.getX()));
      double y = Math.max(Math.abs(startVector.getY()), Math.abs(endVector.getY()));

      maxX = Math.max(x, maxX);
      maxY = Math.max(y, maxY);
    }

    return new Vector2(maxX, maxY);
  }

  public static Vector2 maxVector(List<Shape> shapes) {
    double maxX = Double.NEGATIVE_INFINITY;
    double maxY = Double.NEGATIVE_INFINITY;

    for (Shape shape : shapes) {
      Vector2 startVector = shape.nextVector(0);
      Vector2 endVector = shape.nextVector(1);

      double x = Math.max(startVector.getX(), endVector.getX());
      double y = Math.max(startVector.getY(), endVector.getY());

      maxX = Math.max(x, maxX);
      maxY = Math.max(y, maxY);
    }

    return new Vector2(maxX, maxY);
  }

  public static List<Shape> flip(List<Shape> shapes) {
    List<Shape> flippedShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      flippedShapes.add(shape.scale(new Vector2(1, -1)));
    }

    return flippedShapes;
  }

  public static double height(List<Shape> shapes) {
    double maxY = Double.NEGATIVE_INFINITY;
    double minY = Double.POSITIVE_INFINITY;

    for (Shape shape : shapes) {
      Vector2 startVector = shape.nextVector(0);
      Vector2 endVector = shape.nextVector(1);

      maxY = Math.max(maxY, Math.max(startVector.getY(), endVector.getY()));
      minY = Math.min(minY, Math.min(startVector.getY(), endVector.getY()));
    }

    return Math.abs(maxY - minY);
  }

  public static double width(List<Shape> shapes) {
    double maxX = Double.NEGATIVE_INFINITY;
    double minX = Double.POSITIVE_INFINITY;

    for (Shape shape : shapes) {
      Vector2 startVector = shape.nextVector(0);
      Vector2 endVector = shape.nextVector(1);

      maxX = Math.max(maxX, Math.max(startVector.getX(), endVector.getX()));
      minX = Math.min(minX, Math.min(startVector.getX(), endVector.getX()));
    }

    return Math.abs(maxX - minX);
  }

  public static List<Shape> translate(List<Shape> shapes, Vector2 translation) {
    List<Shape> translatedShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      translatedShapes.add(shape.translate(translation));
    }

    return translatedShapes;
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
