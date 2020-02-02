package graphs;

import shapes.Line;
import shapes.Vector;

import java.util.HashMap;
import java.util.List;
import java.util.Map;


public class Graph {
  private Map<Vector, Node> nodes;

  public Graph(List<Line> lines) {
    this.nodes = new HashMap<>();

    for (Line line : lines) {
      if (!nodes.containsKey(line.getA())) {
        nodes.put(line.getA(), new Node(line.getA()));
      }

      if (!nodes.containsKey(line.getB())) {
        nodes.put(line.getB(), new Node(line.getB()));
      }

      Node nodeA = nodes.get(line.getA());
      Node nodeB = nodes.get(line.getB());

      nodeA.addAdjacent(nodeB, line.length);
      nodeB.addAdjacent(nodeA, line.length);
    }
  }
}
