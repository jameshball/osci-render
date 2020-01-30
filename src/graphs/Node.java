package graphs;

import shapes.Point;

public class Node {
  private Point location;

  public Node(Point location) {
    this.location = location;
  }

  public Point getLocation() {
    return location;
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
