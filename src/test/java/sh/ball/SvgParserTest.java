package sh.ball;

import static org.junit.Assert.assertEquals;

import java.io.IOException;
import java.net.URISyntaxException;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;
import org.junit.Test;
import org.xml.sax.SAXException;
import sh.ball.parser.svg.SvgParser;
import sh.ball.shapes.Line;
import sh.ball.shapes.Shape;
import sh.ball.util.ClassLoaderUtil;

public class SvgParserTest {

  private String getResource(String path) throws URISyntaxException {
    return ClassLoaderUtil.getResource(path, this.getClass()).toURI().getRawPath();
  }

  @Test
  public void lineToGeneratesALineShape()
      throws ParserConfigurationException, SAXException, IOException, URISyntaxException {
    FrameSet<List<Shape>> frames = new SvgParser(getResource("images/line-to.svg")).parse();
    assertEquals(frames.next(), Line.pathToLines(0.5, 0.5, 0.75, 1, 0, 0, 0.5, 0.5));
  }

  @Test
  public void horizontalLineToGeneratesAHorizontalLineShape()
      throws ParserConfigurationException, SAXException, IOException, URISyntaxException {
    FrameSet<List<Shape>> frames = new SvgParser(getResource("images/horizontal-line-to.svg")).parse();
    assertEquals(frames.next(), Line.pathToLines(0.5, 0.5, 0.75, 0.5, 0, 0.5, 0.5, 0.5));
  }

  @Test
  public void verticalLineToGeneratesAVerticalLineShape()
      throws ParserConfigurationException, SAXException, IOException, URISyntaxException {
    FrameSet<List<Shape>> frames = new SvgParser(getResource("images/vertical-line-to.svg")).parse();
    assertEquals(frames.next(), Line.pathToLines(0.5, 0.5, 0.5, 0.75, 0.5, 0, 0.5, 0.5));
  }

  @Test
  public void closingASubPathDrawsLineToInitialPoint()
      throws ParserConfigurationException, SAXException, IOException, URISyntaxException {
    FrameSet<List<Shape>> frames = new SvgParser(getResource("images/closing-subpath.svg")).parse();
    assertEquals(frames.next(), Line.pathToLines(0.5, 0.5, 0.75, 0.5, 0.75, 0.75, 0.5, 0.75, 0.5, 0.5));
  }
}
