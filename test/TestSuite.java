import org.junit.Test;
import shapes.Line;

import static org.junit.Assert.*;

public class TestSuite {
  // TODO: Create tests for shapes.Shapes class.

  @Test
  public void lineRotationTest1() {
    Line line = new Line(-0.5, 0.5, 0.5, 0.5);

    assertEquals(new Line(-0.5, -0.5, -0.5, 0.5), line.rotate(Math.PI / 2));
    assertEquals(new Line(0.5, -0.5, -0.5, -0.5), line.rotate(Math.PI));
    assertEquals(new Line(0.5, 0.5, 0.5, -0.5), line.rotate(3 * Math.PI / 2));
    assertEquals(new Line(-0.5, 0.5, 0.5, 0.5), line.rotate(2 * Math.PI));
  }

  @Test
  public void lineRotationTest2() {
    Line line = new Line(-0.5, -0.5, -0.25, 0.5);

    assertEquals(new Line(0.5, -0.5, -0.5, -0.25), line.rotate(Math.PI / 2));
  }
}