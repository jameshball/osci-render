public class Line {
  private Point a;
  private Point b;

  public Line(Point a, Point b) {
    this.a = a;
    this.b = b;
  }

  public Line(double x1, double y1, double x2, double y2) {
    this.a = new Point(x1, y1);
    this.b = new Point(x2, y2);
  }

  public double length() {
    return Math.sqrt(Math.pow(getX1() - getX2(), 2) + Math.pow(getY1() - getY2(), 2));
  }

  public Line rotate(double theta) {
    double x1 = getX1();
    double y1 = getY1();
    double x2 = getX2();
    double y2 = getY2();

    Line line = new Line(x1, y1, x2, y2);

    line.setX1(x1 * Math.cos(theta) - y1 * Math.sin(theta));
    line.setY1(x1 * Math.sin(theta) + y1 * Math.cos(theta));
    line.setX2(x2 * Math.cos(theta) - y2 * Math.sin(theta));
    line.setY2(x2 * Math.sin(theta) + y2 * Math.cos(theta));

    return line;
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
