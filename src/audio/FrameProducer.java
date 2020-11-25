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

  private final BlockingQueue<List<Shape>> frameQueue;

  private ObjParser objParser;
  private SvgParser svgParser;
  private TextParser textParser;
  private FileParser parser;

  public FrameProducer(BlockingQueue<List<Shape>> frameQueue) {
    this.frameQueue = frameQueue;
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
}
