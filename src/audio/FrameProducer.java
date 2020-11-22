package audio;

import java.io.IOException;
import java.util.List;
import java.util.concurrent.BlockingQueue;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import parser.FileParser;
import parser.ObjParser;
import parser.TextParser;
import parser.svg.SvgParser;
import shapes.Shape;

public class FrameProducer implements Runnable {

  private static final String DEFAULT_FILE = "models/cube.obj";

  private final BlockingQueue<List<Shape>> frameQueue;

  private ObjParser objParser;
  private SvgParser svgParser;
  private TextParser textParser;
  private FileParser parser;

  public FrameProducer(BlockingQueue<List<Shape>> frameQueue)
      throws ParserConfigurationException, SAXException, IOException {
    this.frameQueue = frameQueue;
    setParser(DEFAULT_FILE);
  }

  @Override
  public void run() {
    while (true) {
      try {
        frameQueue.put(parser.nextFrame());
      } catch (InterruptedException e) {
        e.printStackTrace();
        System.out.println("Frame missed.");
      }
    }
  }

  public void setFocalLength(float focalLength) {
    objParser.setFocalLength(focalLength);
  }

  public void setParser(String filePath)
      throws IOException, ParserConfigurationException, SAXException {
    if (filePath.matches(".*\\.obj")) {
      objParser = new ObjParser(filePath, 1);
      parser = objParser;
    } else if (filePath.matches(".*\\.svg")) {
      svgParser = new SvgParser(filePath);
      parser = svgParser;
    } else if (filePath.matches(".*\\.txt")) {
      textParser = new TextParser(filePath);
      parser = textParser;
    } else {
      throw new IllegalArgumentException(
          "Provided file extension in file " + filePath + " not supported.");
    }
  }
}
