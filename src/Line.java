public class Line {
  private Point a;
  private Point b;

  public Line(Point a, Point b) {
    this.a = a;
    this.b = b;
  }

  public Line(float x1, float y1, float x2, float y2) {
    this.a = new Point(x1, y1);
    this.b = new Point(x2, y2);
  }

  public float length() {
    return (float) Math.sqrt(Math.pow(getX1() - getX2(), 2) + Math.pow(getY1() - getY2(), 2));
  }

  public Point getA() {
    return a;
  }

  public Point getB() {
    return b;
  }

  public float getX1() {
    return a.getX();
  }

  public float getY1() {
    return a.getY();
  }

  public float getX2() {
    return b.getX();
  }

  public float getY2() {
    return b.getY();
  }
}
