package shapes;

public class Line {
  private Point a;
  private Point b;
  private double weight;

  public static final double DEFAULT_WEIGHT = 100;

  public Line(Point a, Point b, double weight) {
    this.a = a;
    this.b = b;
    this.weight = weight;
  }

  public Line(Point a, Point b) {
    this(a, b, DEFAULT_WEIGHT);
  }

  public Line(double x1, double y1, double x2, double y2, double weight) {
    this(new Point(x1, y1), new Point(x2, y2), weight);
  }

  public Line(double x1, double y1, double x2, double y2) {
    this(new Point(x1, y1), new Point(x2, y2));
  }

  public double length() {
    return Math.sqrt(Math.pow(getX1() - getX2(), 2) + Math.pow(getY1() - getY2(), 2));
  }

  public Line rotate(double theta) {
    return new Line(getA().rotate(theta), getB().rotate(theta), getWeight());
  }

  public Line copy() {
    return new Line(getA().copy(), getB().copy(), getWeight());
  }

  public Point getA() {
    return a;
  }

  public Point getB() {
    return b;
  }

  public double getX1() {
    return a.getX();
  }

  public double getY1() {
    return a.getY();
  }

  public double getX2() {
    return b.getX();
  }

  public double getY2() {
    return b.getY();
  }

  public void setX1(double x1) {
    a.setX(x1);
  }

  public void setY1(double y1) {
    a.setY(y1);
  }

  public void setX2(double x2) {
    b.setX(x2);
  }

  public void setY2(double y2) {
    b.setY(y2);
  }

  public double getWeight() {
    return weight;
  }

  public void setWeight(double weight) {
    this.weight = weight;
  }

  @Override
  public boolean equals(Object obj) {
    if (obj == this) {
      return true;
    } else if (obj instanceof Line) {
      Line line = (Line) obj;

      return line.getA().equals(getA()) && line.getB().equals(getB());
    } else {
      return false;
    }
  }
}
