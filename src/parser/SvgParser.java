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

public class SvgParser extends FileParser {

  private final List<Shape> shapes;
  private static final Map<Character, Function<List<Float>, List<? extends Shape>>> commandMap = new HashMap<>();

  static {
    fileExtension = "svg";

    commandMap.put('M', SvgParser::parseMoveToAbsolute);
    commandMap.put('m', SvgParser::parseMoveToRelative);
    commandMap.put('L', SvgParser::parseLineToAbsolute);
    commandMap.put('l', SvgParser::parseLineToRelative);
    commandMap.put('H', SvgParser::parseHorizontalLineToAbsolute);
    commandMap.put('h', SvgParser::parseHorizontalLineToRelative);
    commandMap.put('V', SvgParser::parseVerticalLineToAbsolute);
    commandMap.put('v', SvgParser::parseVerticalLineToRelative);
    commandMap.put('C', SvgParser::parseCurveToAbsolute);
    commandMap.put('c', SvgParser::parseCurveToRelative);
    commandMap.put('S', SvgParser::parseSmoothCurveToAbsolute);
    commandMap.put('s', SvgParser::parseSmoothCurveToRelative);
    commandMap.put('Q', SvgParser::parseQuadraticCurveToAbsolute);
    commandMap.put('q', SvgParser::parseQuadraticCurveToRelative);
    commandMap.put('T', SvgParser::parseSmoothQuadraticCurveToAbsolute);
    commandMap.put('t', SvgParser::parseSmoothQuadraticCurveToRelative);
    commandMap.put('A', SvgParser::parseEllipticalArcAbsolute);
    commandMap.put('a', SvgParser::parseEllipticalArcRelative);
    commandMap.put('Z', SvgParser::parseClosePath);
    commandMap.put('z', SvgParser::parseClosePath);
  }

  private static List<? extends Shape> parseMoveToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseMoveToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseClosePath(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseLineToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseLineToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseHorizontalLineToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseHorizontalLineToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseVerticalLineToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseVerticalLineToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseCurveToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseCurveToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseSmoothCurveToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseSmoothCurveToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseQuadraticCurveToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseQuadraticCurveToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseSmoothQuadraticCurveToAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseSmoothQuadraticCurveToRelative(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseEllipticalArcAbsolute(List<Float> args) {
    return null;
  }

  private static List<? extends Shape> parseEllipticalArcRelative(List<Float> args) {
    return null;
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
      String[] commands = preProcessPath(path);

      for (String command : commands) {
        // Split the command into number strings and convert them into floats.
        List<Float> nums = Arrays.stream(command.substring(1).split(" "))
            .map(Float::parseFloat)
            .collect(Collectors.toList());

        // Use the nums to get a list of shapes, using the first character in the command to specify
        // the function to use.
        shapes.addAll(commandMap.get(command.charAt(0)).apply(nums));
      }
    }
  }

  @Override
  public List<Shape> getShapes() {
    return shapes;
  }
}
