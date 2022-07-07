package sh.ball.shapes;

import java.awt.geom.AffineTransform;
import java.awt.geom.PathIterator;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;

public abstract class Shape {

  protected static final double INVALID_LENGTH = -1;

  public abstract Vector2 nextVector(double drawingProgress);

  public abstract Shape rotate(double theta);

  public abstract Shape scale(double factor);

  public abstract Shape scale(Vector2 vector);

  public abstract Shape translate(Vector2 vector);

  public abstract double getLength();

  /* SHAPE HELPER FUNCTIONS */

  public static List<Shape> convert(java.awt.Shape shape) {
    List<Shape> shapes = new ArrayList<>();
    Vector2 moveToPoint = new Vector2();
    Vector2 currentPoint = new Vector2();
    float[] coords = new float[6];

    PathIterator pathIterator = shape.getPathIterator(new AffineTransform());
    while (!pathIterator.isDone()) {
      switch (pathIterator.currentSegment(coords)) {
        case PathIterator.SEG_MOVETO -> {
          currentPoint = new Vector2(coords[0], coords[1]);
          moveToPoint = currentPoint;
        }
        case PathIterator.SEG_LINETO -> {
          shapes.add(new Line(
            currentPoint,
            new Vector2(coords[0], coords[1])
          ));
          currentPoint = new Vector2(coords[0], coords[1]);
        }
        case PathIterator.SEG_QUADTO -> {
          shapes.add(new QuadraticBezierCurve(
            currentPoint,
            new Vector2(coords[0], coords[1]),
            new Vector2(coords[2], coords[3])
          ));
          currentPoint = new Vector2(coords[2], coords[3]);
        }
        case PathIterator.SEG_CUBICTO -> {
          shapes.add(new CubicBezierCurve(
            currentPoint,
            new Vector2(coords[0], coords[1]),
            new Vector2(coords[2], coords[3]),
            new Vector2(coords[4], coords[5])
          ));
          currentPoint = new Vector2(coords[4], coords[5]);
        }
        case PathIterator.SEG_CLOSE ->
          shapes.add(new Line(currentPoint, moveToPoint));
      }
      pathIterator.next();
    }

    return shapes;
  }

  // Normalises sh.ball.shapes between the coords -1 and 1 for proper scaling on an oscilloscope. May not
  // work perfectly with curves that heavily deviate from their start and end points.
  public static List<Shape> normalize(List<Shape> shapes) {
    double oldHeight = height(shapes);
    double oldWidth = width(shapes);
    double maxDim = Math.max(oldHeight, oldWidth);
    List<Shape> normalizedShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      normalizedShapes.add(shape.scale(new Vector2(2 / maxDim, -2 / maxDim)));
    }

    Vector2 maxVector = maxVector(normalizedShapes);
    double height = height(normalizedShapes);

    return translate(normalizedShapes, new Vector2(-1, -maxVector.getY() + height / 2));
  }

  // TODO: Ideally this should cut off any width/height that is less than maxDim.
  // If this is confusing, compare the output of a square viewBox vs non-square.
  public static List<Shape> normalize(List<Shape> shapes, double width, double height) {
    double maxDim = Math.max(width, height);
    List<Shape> normalizedShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      normalizedShapes.add(shape.scale(new Vector2(2 / maxDim, -2 / maxDim)));
    }

    normalizedShapes = translate(normalizedShapes, new Vector2(-1, 1));
    return removeOutOfBounds(normalizedShapes);
  }

  public static List<Shape> removeOutOfBounds(List<Shape> shapes) {
    List<Shape> culledShapes = new ArrayList<>();

    for (Shape shape : shapes) {
      Vector2 start = shape.nextVector(0);
      Vector2 end = shape.nextVector(1);
      if ((start.getX() < 1 && start.getX() > -1) || (start.getY() < 1 && start.getY() > -1)) {
        if ((end.getX() < 1 && end.getX() > -1) || (end.getY() < 1 && end.getY() > -1)) {
          Function<Double, Double> between = (val) -> Math.min(Math.max(val, -1), 1);

          if (shape instanceof Line) {
            Vector2 newStart = new Vector2(between.apply(start.getX()), between.apply(start.getY()));
            Vector2 newEnd = new Vector2(between.apply(end.getX()), between.apply(end.getY()));
            culledShapes.add(new Line(newStart, newEnd));
          } else {
            culledShapes.add(shape);
          }
        }
      }
    }

    return culledShapes;
  }

  public static Vector2 maxAbsVector(List<? extends Shape> shapes) {
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

    Vector2[] vectors = new Vector2[4];

    for (Shape shape : shapes) {
      for (int i = 0; i < vectors.length; i++) {
        vectors[i] = shape.nextVector(i * 1.0 / vectors.length);
      }

      for (Vector2 vector : vectors) {
        maxY = Math.max(maxY, vector.y);
        minY = Math.min(minY, vector.y);
      }
    }

    return Math.abs(maxY - minY);
  }

  public static double width(List<Shape> shapes) {
    double maxX = Double.NEGATIVE_INFINITY;
    double minX = Double.POSITIVE_INFINITY;

    Vector2[] vectors = new Vector2[4];

    for (Shape shape : shapes) {
      for (int i = 0; i < vectors.length; i++) {
        vectors[i] = shape.nextVector(i * 1.0 / vectors.length);
      }

      for (Vector2 vector : vectors) {
        maxX = Math.max(maxX, vector.x);
        minX = Math.min(minX, vector.x);
      }
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

  public static List<Shape> generatePolygram(int sides, int angleJump, Vector2 start) {
    List<Shape> polygon = new ArrayList<>();

    double theta = angleJump * 2 * Math.PI / sides;
    Vector2 rotated = start.rotate(theta);
    polygon.add(new Line(start, rotated));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta)));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }

  public static List<Shape> generatePolygram(int sides, int angleJump, double scale) {
    return generatePolygram(sides, angleJump, new Vector2(scale, scale));
  }

  public static List<Shape> generatePolygon(int sides, Vector2 start) {
    return generatePolygram(sides, 1, start);
  }

  public static List<Shape> generatePolygon(int sides, double scale) {
    return generatePolygon(sides, new Vector2(scale, scale));
  }

  public static double totalLength(List<? extends Shape> shapes) {
    double total = 0;
    for (Shape shape : shapes) {
      total += shape.getLength();
    }
    return total;
  }
}
