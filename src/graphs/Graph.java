package graphs;

import shapes.Line;
import shapes.Point;

import java.util.ArrayList;
import java.util.List;


public class Graph {
  private List<Node> nodes;
  private List<Point> locations;

  public Graph(List<Line> lines) {
    this.nodes = new ArrayList<>();
    this.locations = new ArrayList<>();

    for (Line line : lines) {
      nodes.add(new Node(line.getA()));
      nodes.add(new Node(line.getB()));
    }
  }
}
