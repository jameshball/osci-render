package sh.ball.audio;

import sh.ball.Renderer;
import sh.ball.engine.Vector3;
import java.io.IOException;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import sh.ball.parser.FileParser;
import sh.ball.parser.ObjParser;
import sh.ball.parser.TextParser;
import sh.ball.parser.svg.SvgParser;
import sh.ball.shapes.Shape;

public class FrameProducer implements Runnable {

  private final Renderer<List<Shape>> renderer;

  private ObjParser objParser;
  private SvgParser svgParser;
  private TextParser textParser;
  private FileParser parser;

  public FrameProducer(Renderer<List<Shape>> renderer) {
    this.renderer = renderer;
  }

  @Override
  public void run() {
    while (true) {
      renderer.addFrame(parser.nextFrame());
    }
  }

  public void setParser(String filePath)
      throws IOException, ParserConfigurationException, SAXException {
    if (ObjParser.isObjFile(filePath)) {
      objParser = new ObjParser(filePath, 1);
      parser = objParser;
    } else if (SvgParser.isSvgFile(filePath)) {
      svgParser = new SvgParser(filePath);
      parser = svgParser;
    } else if (TextParser.isTxtFile(filePath)) {
      textParser = new TextParser(filePath);
      parser = textParser;
    } else {
      throw new IllegalArgumentException(
          "Provided file extension in file " + filePath + " not supported.");
    }
  }

  public void setFocalLength(Double focalLength) {
    objParser.setFocalLength(focalLength);
  }

  public void setCameraPos(Vector3 vector) {
    objParser.setCameraPos(vector);
  }
}
