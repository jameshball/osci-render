import org.junit.Test;

import static org.junit.Assert.*;

public class TestSuite {
  @Test
  public void lineRotationTest() {
    Line line = new Line(-0.5, 0.5, 0.5, 0.5);
    line.rotate(Math.PI / 2);

    assertEquals(new Line(0.5, 0.5, 0.5, -0.5), line);
  }
}