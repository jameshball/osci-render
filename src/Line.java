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

  public void rotate(double theta) {
    setX1(getX1() * Math.cos(theta) - getY1() * Math.sin(theta));
    setY1(getX1() * Math.sin(theta) + getY1() * Math.cos(theta));
    setX2(getX2() * Math.cos(theta) - getY2() * Math.sin(theta));
    setY2(getX2() * Math.sin(theta) + getY2() * Math.cos(theta));
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
