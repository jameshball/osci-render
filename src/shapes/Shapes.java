package shapes;

import java.util.ArrayList;
import java.util.List;

public class Shapes {
  public static List<Line> generatePolygram(int sides, int angleJump, Vector start, double weight) {
    List<Line> polygon = new ArrayList<>();

    double theta = angleJump * 2 * Math.PI / sides;

    Vector rotated = start.rotate(theta);

    polygon.add(new Line(start, rotated, weight));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta), weight));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }

  public static List<Line> generatePolygram(int sides, int angleJump, Vector start) {
    return generatePolygram(sides, angleJump, start, Line.DEFAULT_WEIGHT);
  }

  public static List<Line> generatePolygram(int sides, int angleJump, double scale, double weight) {
    return generatePolygram(sides, angleJump, new Vector(scale, scale), weight);
  }

  public static List<Line> generatePolygram(int sides, int angleJump, double scale) {
    return generatePolygram(sides, angleJump, new Vector(scale, scale));
  }

  public static List<Line> generatePolygon(int sides, Vector start, double weight) {
    return generatePolygram(sides, 1, start, weight);
  }

  public static List<Line> generatePolygon(int sides, Vector start) {
    return generatePolygram(sides, 1, start);
  }

  public static List<Line> generatePolygon(int sides, double scale, double weight) {
    return generatePolygon(sides, new Vector(scale, scale), weight);
  }

  public static List<Line> generatePolygon(int sides, double scale) {
    return generatePolygon(sides, new Vector(scale, scale));
  }

  public static List<Line> cleanupLines(List<Line> lines) {
    // TODO: Implement this to create a route inspection of all lines in the input
    //  to minimise electron beam jump distance.
    return null;
  }
}
