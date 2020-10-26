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
  public void lineToGeneratesALineShape()
      throws ParserConfigurationException, SAXException, IOException {
    SvgParser svgParser = new SvgParser("test/images/line-to.svg");
    List<Shape> shapes = svgParser.getShapes();
    assertEquals(shapes.get(0), new Line(new Vector2(0.5, 0.5), new Vector2(0.75, 1)));
    assertEquals(shapes.get(1), new Line(new Vector2(0.75, 1), new Vector2(0, 0)));
    assertEquals(shapes.get(2), new Line(new Vector2(0, 0), new Vector2(0.5, 0.5)));
  }

  @Test
  public void closingASubPathDrawsLineToInitialPoint()
      throws ParserConfigurationException, SAXException, IOException {
    SvgParser svgParser = new SvgParser("test/images/closing-subpath.svg");
    List<Shape> shapes = svgParser.getShapes();
    assertEquals(shapes.get(0), new Line(new Vector2(0.5, 0.5), new Vector2(0.75, 0.5)));
    assertEquals(shapes.get(1), new Line(new Vector2(0.75, 0.5), new Vector2(0.75, 0.75)));
    assertEquals(shapes.get(2), new Line(new Vector2(0.75, 0.75), new Vector2(0.5, 0.75)));
    assertEquals(shapes.get(3), new Line(new Vector2(0.5, 0.75), new Vector2(0.5, 0.5)));
  }
}
