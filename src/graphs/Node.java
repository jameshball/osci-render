package graphs;

import shapes.Point;

import java.util.ArrayList;
import java.util.List;

public class Node {
  private Point location;
  private List<Node> adjacentNodes;

  public Node(Point location) {
    this.location = location;
    this.adjacentNodes = new ArrayList<>();
  }

  public Point getLocation() {
    return location;
  }

  public void addAdjacent(Node node) {
    adjacentNodes.add(node);
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
