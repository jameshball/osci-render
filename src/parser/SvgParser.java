package parser;

import static parser.XmlUtil.asList;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import org.w3c.dom.Document;
import org.w3c.dom.Node;
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

  private Document getSvgDocument(String path)
      throws IOException, SAXException, ParserConfigurationException {
    // opens XML reader for svg file.
    DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
    factory.setValidating(true);
    factory.setIgnoringElementContentWhitespace(true);
    DocumentBuilder builder = factory.newDocumentBuilder();
    File file = new File(path);

    return builder.parse(file);
  }

  private String[] preProcessPath(String path) throws IllegalArgumentException {
    // Replace all commas with spaces and then remove unnecessary whitespace
    path = path.replace(',', ' ');
    path = path.replaceAll("\\s+", " ");
    path = path.replaceAll("(^\\s|\\s$)", "");

    // If there are any characters in the path that are illegal
    if (path.matches("[^mlhvcsqtazMLHVCSQTAZ\\-.\\d\\s]")) {
      throw new IllegalArgumentException("Illegal characters in SVG path.");
    // If there are more than 1 letters or delimiters next to one another
    } else if (path.matches("[a-zA-Z.\\-]{2,}")) {
      throw new IllegalArgumentException("Multiple letters or delimiters found next to one another in SVG path.");
    // First character in path must be a command
    } else if (path.matches("^[a-zA-Z]")) {
      throw new IllegalArgumentException("Start of SVG path is not a letter.");
    }

    // Split on SVG path characters to get a list of instructions, keeping the SVG commands
    return path.split("(?=[mlhvcsqtazMLHVCSQTAZ])");
  }

  private List<String> getSvgPathAttributes(Document svg) {
    List<String> paths = new ArrayList<>();

    for (Node elem : asList(svg.getElementsByTagName("path"))) {
      paths.add(elem.getAttributes().getNamedItem("d").getNodeValue());
    }

    return paths;
  }

  @Override
  protected void parseFile(String filePath)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException {
    Document svg = getSvgDocument(filePath);

    for (String path : getSvgPathAttributes(svg)) {
      preProcessPath(path);
    }
  }

  @Override
  public List<? extends Shape> getShapes() {
    return shapes;
  }
}
