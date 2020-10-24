package parser;

import static parser.XmlUtil.asList;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import org.w3c.dom.Document;
import org.w3c.dom.Node;
import org.xml.sax.SAXException;
import shapes.Shape;
import shapes.Vector2;

public class SvgParser extends FileParser {

  private final List<Shape> shapes;
  private final Map<Character, Function<List<Float>, List<? extends Shape>>> commandMap;

  private Vector2 pos;

  static {
    fileExtension = "svg";
  }

  public SvgParser(String path) throws IOException, SAXException, ParserConfigurationException {
    FileParser.checkFileExtension(path);
    shapes = new ArrayList<>();

    commandMap = new HashMap<>();
    commandMap.put('M', this::parseMoveToAbsolute);
    commandMap.put('m', this::parseMoveToRelative);
    commandMap.put('L', this::parseLineToAbsolute);
    commandMap.put('l', this::parseLineToRelative);
    commandMap.put('H', this::parseHorizontalLineToAbsolute);
    commandMap.put('h', this::parseHorizontalLineToRelative);
    commandMap.put('V', this::parseVerticalLineToAbsolute);
    commandMap.put('v', this::parseVerticalLineToRelative);
    commandMap.put('C', this::parseCurveToAbsolute);
    commandMap.put('c', this::parseCurveToRelative);
    commandMap.put('S', this::parseSmoothCurveToAbsolute);
    commandMap.put('s', this::parseSmoothCurveToRelative);
    commandMap.put('Q', this::parseQuadraticCurveToAbsolute);
    commandMap.put('q', this::parseQuadraticCurveToRelative);
    commandMap.put('T', this::parseSmoothQuadraticCurveToAbsolute);
    commandMap.put('t', this::parseSmoothQuadraticCurveToRelative);
    commandMap.put('A', this::parseEllipticalArcAbsolute);
    commandMap.put('a', this::parseEllipticalArcRelative);

    parseFile(path);
  }

  private List<? extends Shape> parseMoveToAbsolute(List<Float> args) {
    return new ArrayList<>();
  }

  private List<? extends Shape> parseMoveToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseLineToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseLineToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseHorizontalLineToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseHorizontalLineToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseVerticalLineToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseVerticalLineToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseCurveToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseCurveToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseSmoothCurveToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseSmoothCurveToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseQuadraticCurveToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseQuadraticCurveToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseSmoothQuadraticCurveToAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseSmoothQuadraticCurveToRelative(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseEllipticalArcAbsolute(List<Float> args) {
    return null;
  }

  private List<? extends Shape> parseEllipticalArcRelative(List<Float> args) {
    return null;
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
      throw new IllegalArgumentException(
          "Multiple letters or delimiters found next to one another in SVG path.");
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

    // Get all d attributes within path elements in the SVG file.
    for (String path : getSvgPathAttributes(svg)) {
      pos = new Vector2();
      String[] commands = preProcessPath(path);

      for (String command : commands) {
        char commandChar = command.charAt(0);

        if (commandChar == 'z' || commandChar == 'Z') {
          break;
        }

        // Split the command into number strings and convert them into floats.
        List<Float> nums = Arrays.stream(command.substring(1).split(" "))
            .map(Float::parseFloat)
            .collect(Collectors.toList());

        // Use the nums to get a list of shapes, using the first character in the command to specify
        // the function to use.
        shapes.addAll(commandMap.get(commandChar).apply(nums));
      }
    }
  }

  @Override
  public List<Shape> getShapes() {
    return shapes;
  }
}
