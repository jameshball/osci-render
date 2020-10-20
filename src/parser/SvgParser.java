package parser;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import org.w3c.dom.Document;
import org.xml.sax.SAXException;
import shapes.Shape;

public class SvgParser extends FileParser {

  private final List<? extends Shape> shapes;

  static {
    fileExtension = "svg";
  }

  public SvgParser(String path) throws IOException, SAXException, ParserConfigurationException {
    super(path);
    shapes = new ArrayList<>();
  }

  @Override
  protected void parseFile(String path)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException {
    DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
    factory.setValidating(true);
    factory.setIgnoringElementContentWhitespace(true);
    DocumentBuilder builder = factory.newDocumentBuilder();
    File file = new File(path);
    Document doc = builder.parse(file);
  }

  @Override
  public List<? extends Shape> getShapes() {
    return shapes;
  }
}
