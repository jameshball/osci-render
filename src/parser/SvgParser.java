package parser;

import static parser.XmlUtil.asList;
import static parser.XmlUtil.getAttributesOnTags;
import static parser.XmlUtil.getNodeValue;
import static parser.XmlUtil.getXMLDocument;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.management.modelmbean.XMLParseException;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import org.w3c.dom.Document;
import org.w3c.dom.Node;
import org.xml.sax.SAXException;
import shapes.CubicBezierCurve;
import shapes.Line;
import shapes.QuadraticBezierCurve;
import shapes.Shape;
import shapes.Vector2;

public class SvgParser extends FileParser {

  private final Map<Character, Function<List<Float>, List<Shape>>> commandMap;

  private List<Shape> shapes;

  private Vector2 currPoint;
  private Vector2 initialPoint;
  private Vector2 prevCubicControlPoint;
  private Vector2 prevQuadraticControlPoint;
  private Document svg;

  public SvgParser(String path) throws IOException, SAXException, ParserConfigurationException {
    checkFileExtension(path);
    shapes = new ArrayList<>();
    commandMap = new HashMap<>();
    initialiseCommandMap();
    parseFile(path);
  }

  // Map command chars to function calls.
  private void initialiseCommandMap() {
    // TODO: Pull out into builder-like syntax if possible
    commandMap.put('M', (args) -> parseMoveTo(args, true));
    commandMap.put('m', (args) -> parseMoveTo(args, false));
    commandMap.put('L', (args) -> parseLineTo(args, true, true, true));
    commandMap.put('l', (args) -> parseLineTo(args, false, true, true));
    commandMap.put('H', (args) -> parseLineTo(args, true, true, false));
    commandMap.put('h', (args) -> parseLineTo(args, false, true, false));
    commandMap.put('V', (args) -> parseLineTo(args, true, false, true));
    commandMap.put('v', (args) -> parseLineTo(args, false, false, true));
    commandMap.put('C', (args) -> parseCurveTo(args, true, true, false));
    commandMap.put('c', (args) -> parseCurveTo(args, false, true, false));
    commandMap.put('S', (args) -> parseCurveTo(args, true, true, true));
    commandMap.put('s', (args) -> parseCurveTo(args, false, true, true));
    commandMap.put('Q', (args) -> parseCurveTo(args, true, false, false));
    commandMap.put('q', (args) -> parseCurveTo(args, false, false, false));
    commandMap.put('T', (args) -> parseCurveTo(args, true, false, true));
    commandMap.put('t', (args) -> parseCurveTo(args, false, false, true));
    commandMap.put('A', (args) -> parseEllipticalArc(args, true));
    commandMap.put('a', (args) -> parseEllipticalArc(args, false));
    commandMap.put('Z', this::parseClosePath);
    commandMap.put('z', this::parseClosePath);
  }

  // Does error checking against SVG path and returns array of SVG commands and arguments
  private String[] preProcessPath(String path) throws IllegalArgumentException {
    // Replace all commas with spaces and then remove unnecessary whitespace
    path = path.replace(',', ' ');
    path = path.replace("-", " -");
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

  private static List<Float> splitCommand(String command) {
    List<Float> nums = new ArrayList<>();
    String[] decimalSplit = command.split("\\.");

    try {
      if (decimalSplit.length == 1) {
        nums.add(Float.parseFloat(decimalSplit[0]));
      } else {
        nums.add(Float.parseFloat(decimalSplit[0] + "." + decimalSplit[1]));

        for (int i = 2; i < decimalSplit.length; i++) {
          nums.add(Float.parseFloat("." + decimalSplit[i]));
        }
      }
    } catch (Exception e) {
      System.out.println(Arrays.toString(decimalSplit));
      System.out.println(command);
    }

    return nums;
  }

  @Override
  protected String getFileExtension() {
    return "svg";
  }

  @Override
  protected void parseFile(String filePath)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException {
    this.svg = getXMLDocument(filePath);
    List<Node> svgElem = asList(svg.getElementsByTagName("svg"));

    if (svgElem.size() != 1) {
      throw new IllegalArgumentException("SVG has either zero or more than one svg element.");
    }

    // Get all d attributes within path elements in the SVG file.
    for (Node node : getAttributesOnTags(svg, "path", "d")) {
      shapes.addAll(parsePath(node.getNodeValue()));
    }

    shapes = Shape.normalize(shapes);
  }

  public List<Shape> parseGlyphsWithUnicode(char unicode) {
    String xmlEntityHex = "&#x" + String.format("%x", (int) unicode) + ";";

    for (Node node : asList(svg.getElementsByTagName("glyph"))) {
      String unicodeString = getNodeValue(node, "unicode");

      if (unicodeString == null) {
        throw new IllegalArgumentException("Glyph should have unicode attribute.");
      }

      if (unicodeString.equals(xmlEntityHex) || (unicodeString.length() == 1 && unicodeString.charAt(0) == unicode)) {
        return parsePath(getNodeValue(node, "d"));
      }
    }

    return List.of();
  }

  // Performs path parsing on a single d=path
  private List<Shape> parsePath(String path) {
    if (path == null) {
      return List.of();
    }

    currPoint = new Vector2();
    prevCubicControlPoint = null;
    prevQuadraticControlPoint = null;
    String[] commands = preProcessPath(path);
    List<Shape> svgShapes = new ArrayList<>();

    for (String command : commands) {
      char commandChar = command.charAt(0);
      List<Float> nums = null;

      if (commandChar != 'z' && commandChar != 'Z') {
        // Split the command into number strings and convert them into floats.
        nums = Arrays.stream(command.substring(1).split(" "))
            .filter(Predicate.not(String::isBlank))
            .flatMap((numString) -> splitCommand(numString).stream())
            .collect(Collectors.toList());
      }

      // Use the nums to get a list of shapes, using the first character in the command to specify
      // the function to use.
      svgShapes.addAll(commandMap.get(commandChar).apply(nums));

      if (!String.valueOf(commandChar).matches("[csCS]")) {
        prevCubicControlPoint = null;
      }
      if (!String.valueOf(commandChar).matches("[qtQT]")) {
        prevQuadraticControlPoint = null;
      }
    }

    return svgShapes;
  }

  @Override
  public List<Shape> nextFrame() {
    return shapes;
  }

  // Parses moveto commands (M and m commands)
  private List<Shape> parseMoveTo(List<Float> args, boolean isAbsolute) {
    if (args.size() % 2 != 0 || args.size() < 2) {
      throw new IllegalArgumentException("SVG moveto command has incorrect number of arguments.");
    }

    Vector2 vec = new Vector2(args.get(0), args.get(1));

    if (isAbsolute) {
      currPoint = vec;
      initialPoint = currPoint;
      if (args.size() > 2) {
        return parseLineTo(args.subList(2, args.size() - 1), true, true, true);
      }
    } else {
      currPoint = currPoint.translate(vec);
      initialPoint = currPoint;
      if (args.size() > 2) {
        return parseLineTo(args.subList(2, args.size() - 1), false, true, true);
      }
    }

    return new ArrayList<>();
  }

  // Parses close path commands (Z and z commands)
  private List<Shape> parseClosePath(List<Float> args) {
    if (!currPoint.equals(initialPoint)) {
      Line line = new Line(currPoint, initialPoint);
      currPoint = initialPoint;
      return List.of(line);
    } else {
      return List.of();
    }
  }

  // Parses lineto commands (L, l, H, h, V, and v commands)
  // isHorizontal and isVertical should be true for parsing L and l commands
  // Only isHorizontal should be true for parsing H and h commands
  // Only isVertical should be true for parsing V and v commands
  private List<Shape> parseLineTo(List<Float> args, boolean isAbsolute,
      boolean isHorizontal, boolean isVertical) {
    int expectedArgs = isHorizontal && isVertical ? 2 : 1;

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
      throw new IllegalArgumentException("SVG lineto command has incorrect number of arguments.");
    }

    List<Shape> lines = new ArrayList<>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
      Vector2 newPoint;

      if (expectedArgs == 1) {
        newPoint = new Vector2(args.get(i), args.get(i));
      } else {
        newPoint = new Vector2(args.get(i), args.get(i + 1));
      }

      if (isHorizontal && !isVertical) {
        newPoint = isAbsolute ? newPoint.setY(currPoint.getY()) : newPoint.setY(0);
      } else if (isVertical && !isHorizontal) {
        newPoint = isAbsolute ? newPoint.setX(currPoint.getX()) : newPoint.setX(0);
      }

      if (!isAbsolute) {
        newPoint = currPoint.translate(newPoint);
      }

      lines.add(new Line(currPoint, newPoint));
      currPoint = newPoint;
    }

    return lines;
  }

  // Parses curveto commands (C, c, S, s, Q, q, T, and t commands)
  // isCubic should be true for parsing C, c, S, and s commands
  // isCubic should be false for parsing Q, q, T, and t commands
  // isSmooth should be true for parsing S, s, T, and t commands
  // isSmooth should be false for parsing C, c, Q, and q commands
  private List<Shape> parseCurveTo(List<Float> args, boolean isAbsolute, boolean isCubic,
      boolean isSmooth) {
    int expectedArgs = isCubic ? 4 : 2;
    if (!isSmooth) {
      expectedArgs += 2;
    }

    if (args.size() % expectedArgs != 0 || args.size() < expectedArgs) {
      throw new IllegalArgumentException("SVG curveto command has incorrect number of arguments.");
    }

    List<Shape> curves = new ArrayList<>();

    for (int i = 0; i < args.size(); i += expectedArgs) {
      Vector2 controlPoint1;
      Vector2 controlPoint2 = new Vector2();

      if (isSmooth) {
        if (isCubic) {
          controlPoint1 = prevCubicControlPoint == null ? currPoint
              : prevCubicControlPoint.reflectRelativeToVector(currPoint);
        } else {
          controlPoint1 = prevQuadraticControlPoint == null ? currPoint
              : prevQuadraticControlPoint.reflectRelativeToVector(currPoint);
        }
      } else {
        controlPoint1 = new Vector2(args.get(i), args.get(i + 1));
      }

      if (isCubic) {
        controlPoint2 = new Vector2(args.get(i + 2), args.get(i + 3));
      }

      Vector2 newPoint = new Vector2(args.get(i + expectedArgs - 2),
          args.get(i + expectedArgs - 1));

      if (!isAbsolute) {
        controlPoint1 = currPoint.translate(controlPoint1);
        controlPoint2 = currPoint.translate(controlPoint2);
        newPoint = currPoint.translate(newPoint);
      }

      if (isCubic) {
        curves.add(new CubicBezierCurve(currPoint, controlPoint1, controlPoint2, newPoint));
        currPoint = newPoint;
        prevCubicControlPoint = controlPoint2;
      } else {
        curves.add(new QuadraticBezierCurve(currPoint, controlPoint1, newPoint));
        currPoint = newPoint;
        prevQuadraticControlPoint = controlPoint1;
      }
    }

    return curves;
  }

  private List<Shape> parseEllipticalArc(List<Float> args, boolean isAbsolute) {
    // TODO: Properly implement

    if (args.size() % 7 != 0 || args.size() < 7) {
      throw new IllegalArgumentException(
          "SVG elliptical arc command has incorrect number of arguments.");
    }

    List<Float> lineToArgs = new ArrayList<>();

    for (int i = 0; i < args.size(); i += 7) {
      lineToArgs.add(args.get(i + 5));
      lineToArgs.add(args.get(i + 6));
    }

    return parseLineTo(lineToArgs, isAbsolute, true, true);
  }
}
