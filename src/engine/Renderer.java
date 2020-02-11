package engine;

import shapes.Line;
import shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public abstract class Renderer {
  public List<Line> getFrame(List<Vector2> vertices, List<Integer> connections) {
    List<Line> lines = new ArrayList<>();

    for (int i = 0; i < connections.size(); i+=2) {
      Vector2 start = vertices.get(connections.get(i));
      Vector2 end = vertices.get(connections.get(i+1));
      double x1 = start.getX();
      double y1 = start.getY();
      double x2 = end.getX();
      double y2 = end.getY();

      lines.add(new Line(x1, y1, x2, y2));
    }

    return lines;
  }
}
