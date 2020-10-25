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
import shapes.CubicBezierCurve;
import shapes.Line;
import shapes.QuadraticBezierCurve;
import shapes.Shape;
import shapes.Vector2;

public class SvgParser extends FileParser {

  private final List<Shape> shapes;
  private final Map<Character, Function<List<Float>, List<? extends Shape>>> commandMap;

  private Vector2 currPoint;
  private Vector2 initialPoint;
  private Vector2 prevCubicControlPoint;
  private Vector2 prevQuadraticControlPoint;

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
    commandMap.put('C', this::parseCubicCurveToAbsolute);
    commandMap.put('c', this::parseCubicCurveToRelative);
    commandMap.put('S', this::parseSmoothCurveToAbsolute);
    commandMap.put('s', this::parseSmoothCurveToRelative);
    commandMap.put('Q', this::parseQuadraticCurveToAbsolute);
    commandMap.put('q', this::parseQuadraticCurveToRelative);
    commandMap.put('T', this::parseSmoothQuadraticCurveToAbsolute);
    commandMap.put('t', this::parseSmoothQuadraticCurveToRelative);
    commandMap.put('A', this::parseEllipticalArcAbsolute);
    commandMap.put('a', this::parseEllipticalArcRelative);
    commandMap.put('Z', this::parseClosePath);
    commandMap.put('z', this::parseClosePath);

    parseFile(path);
  }

  private List<? extends Shape> parseMoveToAbsolute(List<Float> args) {
    return parseMoveTo(args, true);
  }

  private List<? extends Shape> parseMoveToRelative(List<Float> args) {
    return parseMoveTo(args, false);
  }

  private List<? extends Shape> parseMoveTo(List<Float> args, boolean isAbsolute) {
    if (args.size() % 2 != 0 || args.size() < 2) {
      throw new IllegalArgumentException("SVG moveto command has incorrect number of arguments.");
    }

    Vector2 vec = new Vector2(args.get(0), args.get(1));

    if (isAbsolute) {
      currPoint = vec;
      initialPoint = currPoint;
      if (args.size() > 2) {
        return parseLineToAbsolute(args.subList(2, args.size() - 1));
      }
    } else {
      currPoint = currPoint.translate(vec);
      initialPoint = currPoint;
      if (args.size() > 2) {
        return parseLineToRelative(args.subList(2, args.size() - 1));
      }
    }

    return new ArrayList<>();
  }

  private List<? extends Shape> parseClosePath(List<Float> args) {
    Line line = new Line(currPoint, initialPoint);
    currPoint = initialPoint;
    return List.of(line);
  }

  private List<? extends Shape> parseLineToAbsolute(List<Float> args) {
    return parseLineTo(args, true);
  }

  private List<? extends Shape> parseLineToRelative(List<Float> args) {
    return parseLineTo(args, false);
  }

  private List<? extends Shape> parseLineTo(List<Float> args, boolean isAbsolute) {
    if (args.size() % 2 != 0 || args.size() < 2) {
      throw new IllegalArgumentException("SVG lineto command has incorrect number of arguments.");
    }

    List<Line> lines = new ArrayList<>();

    for (int i = 0; i < args.size(); i += 2) {
      Vector2 newPoint = new Vector2(args.get(i), args.get(i + 1));

      if (!isAbsolute) {
        newPoint = currPoint.translate(newPoint);
      }

      lines.add(new Line(currPoint, newPoint));
      currPoint = newPoint;
    }

    return lines;
  }

  private List<? extends Shape> parseHorizontalLineToAbsolute(List<Float> args) {
    return parseHorizontalLineTo(args, true);
  }

  private List<? extends Shape> parseHorizontalLineToRelative(List<Float> args) {
    return parseHorizontalLineTo(args, false);
  }

  private List<? extends Shape> parseHorizontalLineTo(List<Float> args, boolean isAbsolute) {
    List<Line> lines = new ArrayList<>();

    for (Float point : args) {
      Vector2 newPoint;

      if (isAbsolute) {
        newPoint = new Vector2(point, currPoint.getY());
      } else {
        newPoint = currPoint.translate(new Vector2(point, 0));
      }

      lines.add(new Line(currPoint, newPoint));
      currPoint = newPoint;
    }

    return lines;
  }

  private List<? extends Shape> parseVerticalLineToAbsolute(List<Float> args) {
    return parseVerticalLineTo(args, true);
  }

  private List<? extends Shape> parseVerticalLineToRelative(List<Float> args) {
    return parseVerticalLineTo(args, false);
  }

  private List<? extends Shape> parseVerticalLineTo(List<Float> args, boolean isAbsolute) {
    List<Line> lines = new ArrayList<>();

    for (Float point : args) {
      Vector2 newPoint;

      if (isAbsolute) {
        newPoint = new Vector2(currPoint.getX(), point);
      } else {
        newPoint = currPoint.translate(new Vector2(0, point));
      }

      lines.add(new Line(currPoint, newPoint));
      currPoint = newPoint;
    }

    return lines;
  }

  private List<? extends Shape> parseCubicCurveToAbsolute(List<Float> args) {
    return parseCurveTo(args, true, true, false);
  }

  private List<? extends Shape> parseCubicCurveToRelative(List<Float> args) {
    return parseCurveTo(args, false, true, false);
  }

  private List<? extends Shape> parseSmoothCurveToAbsolute(List<Float> args) {
    return parseCurveTo(args, true, true, true);
  }

  private List<? extends Shape> parseSmoothCurveToRelative(List<Float> args) {
    return parseCurveTo(args, false, true, true);
  }

  private List<? extends Shape> parseQuadraticCurveToAbsolute(List<Float> args) {
    return parseCurveTo(args, true, false, false);
  }

  private List<? extends Shape> parseQuadraticCurveToRelative(List<Float> args) {
    return parseCurveTo(args, false, false, false);
  }

  private List<? extends Shape> parseSmoothQuadraticCurveToAbsolute(List<Float> args) {
    return parseCurveTo(args, true, false, true);
  }

  private List<? extends Shape> parseSmoothQuadraticCurveToRelative(List<Float> args) {
    return parseCurveTo(args, false, false, true);
  }

  private List<? extends Shape> parseCurveTo(List<Float> args, boolean isAbsolute, boolean isCubic, boolean isSmooth) {
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
          controlPoint1 = prevCubicControlPoint == null ? currPoint : prevCubicControlPoint.reflectRelativeToVector(currPoint);
        } else {
          controlPoint1 = prevQuadraticControlPoint == null ? currPoint : prevQuadraticControlPoint.reflectRelativeToVector(currPoint);
        }
      } else {
        controlPoint1 = new Vector2(args.get(i), args.get(i + 1));
      }

      if (isCubic) {
        controlPoint2 = new Vector2(args.get(i + 2), args.get(i + 3));
      }

      Vector2 newPoint = new Vector2(args.get(i + expectedArgs - 2), args.get(i + expectedArgs - 1));

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

  private List<? extends Shape> parseEllipticalArcAbsolute(List<Float> args) {
    return parseEllipticalArc(args, true);
  }

  private List<? extends Shape> parseEllipticalArcRelative(List<Float> args) {
    return parseEllipticalArc(args, false);
  }

  private List<? extends Shape> parseEllipticalArc(List<Float> args, boolean isAbsolute) {
    if (args.size() % 7 != 0 || args.size() < 7) {
      throw new IllegalArgumentException("SVG elliptical arc command has incorrect number of arguments.");
    }

    List<Float> lineToArgs = new ArrayList<>();

    for (int i = 0; i < args.size(); i += 7) {
      lineToArgs.add(args.get(i + 5));
      lineToArgs.add(args.get(i + 6));
    }

    return parseLineTo(lineToArgs, isAbsolute);
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
      currPoint = new Vector2();
      prevCubicControlPoint = null;
      prevQuadraticControlPoint = null;
      String[] commands = preProcessPath(path);

      for (String command : commands) {
        char commandChar = command.charAt(0);

        if (commandChar == 'z' || commandChar == 'Z') {
          continue;
        }

        // Split the command into number strings and convert them into floats.
        List<Float> nums = Arrays.stream(command.substring(1).split(" "))
            .map(Float::parseFloat)
            .collect(Collectors.toList());

        // Use the nums to get a list of shapes, using the first character in the command to specify
        // the function to use.
        shapes.addAll(commandMap.get(commandChar).apply(nums));

        if (!String.valueOf(commandChar).matches("[csCS]")) {
          prevCubicControlPoint = null;
        }
        if (!String.valueOf(commandChar).matches("[qtQT]")) {
          prevQuadraticControlPoint = null;
        }
      }
    }
  }

  @Override
  public List<Shape> getShapes() {
    return shapes;
  }
}
