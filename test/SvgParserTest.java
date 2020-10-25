import static org.junit.Assert.assertEquals;

import java.io.IOException;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;
import org.junit.Test;
import org.xml.sax.SAXException;
import parser.SvgParser;
import shapes.Line;
import shapes.Shape;
import shapes.Vector2;

public class SvgParserTest {

  @Test
  public void closingASubPathDrawsLineToInitialPoint()
      throws ParserConfigurationException, SAXException, IOException {
    SvgParser svgParser = new SvgParser("test/images/square-relative.svg");
    List<Shape> shapes = svgParser.getShapes();
    assertEquals(shapes.get(0), new Line(new Vector2(10, 10), new Vector2(12, 10)));
    assertEquals(shapes.get(1), new Line(new Vector2(12, 10), new Vector2(12, 12)));
    assertEquals(shapes.get(2), new Line(new Vector2(12, 12), new Vector2(10, 12)));
    assertEquals(shapes.get(3), new Line(new Vector2(10, 12), new Vector2(10, 10)));
  }
}
