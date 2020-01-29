import java.util.ArrayList;
import java.util.List;

public class Shapes {
  public static List<Line> generatePolygram(int sides, int angleJump, Point start) {
    List<Line> polygon = new ArrayList<>();

    double theta = angleJump * 2 * Math.PI / sides;

    Point rotated = start.rotate(theta);

    polygon.add(new Line(start, rotated));

    while (!rotated.equals(start)) {
      polygon.add(new Line(rotated.copy(), rotated.rotate(theta)));

      rotated = rotated.rotate(theta);
    }

    return polygon;
  }

  public static List<Line> generatePolygram(int sides, int angleJump, double scale) {
    return generatePolygram(sides, angleJump, new Point(scale, scale));
  }

  public static List<Line> generatePolygon(int sides, Point start) {
    return generatePolygram(sides, 1, start);
  }

  public static List<Line> generatePolygon(int sides, double scale) {
    return generatePolygon(sides, new Point(scale, scale));
  }
}
