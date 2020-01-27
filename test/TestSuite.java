import org.junit.Test;

import static org.junit.Assert.*;

public class TestSuite {
  @Test
  public void lineRotationTest1() {
    Line line = new Line(-0.5, 0.5, 0.5, 0.5);

    line.rotate(Math.PI / 2);
    assertEquals(new Line(-0.5, -0.5, -0.5, 0.5), line);

    line.rotate(Math.PI / 2);
    assertEquals(new Line(0.5, -0.5, -0.5, -0.5), line);

    line.rotate(Math.PI / 2);
    assertEquals(new Line(0.5, 0.5, 0.5, -0.5), line);

    line.rotate(Math.PI / 2);
    assertEquals(new Line(-0.5, 0.5, 0.5, 0.5), line);
  }

  @Test
  public void lineRotationTest2() {
    Line line = new Line(-0.5, -0.5, -0.25, 0.5);

    line.rotate(Math.PI / 2);
    assertEquals(new Line(0.5, -0.5, -0.5, -0.25), line);
  }
}