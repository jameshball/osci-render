package graphs;

import shapes.Point;

import java.util.ArrayList;
import java.util.List;

public class Node {
  private Point location;
  private List<Node> adjacentNodes;
  private List<Double> adjacentWeights;

  public Node(Point location) {
    this.location = location;
    this.adjacentNodes = new ArrayList<>();
    this.adjacentWeights = new ArrayList<>();
  }

  public Point getLocation() {
    return location;
  }

  public void addAdjacent(Node node, double weight) {
    adjacentNodes.add(node);
    adjacentWeights.add(weight);
  }

  public Node getAdjacentNode(int index) {
    return adjacentNodes.get(index);
  }

  public double getAdjacentWeight(int index) {
    return adjacentWeights.get(index);
  }

  public int degree() {
    return adjacentNodes.size();
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Node) {
      Node point = (Node) obj;

      return getLocation().equals(point.getLocation());
    } else {
      return false;
    }
  }
}
