package sh.ball.parser.svg;

import static sh.ball.parser.XmlUtil.asList;
import static sh.ball.parser.XmlUtil.getAttributesOnTags;
import static sh.ball.parser.XmlUtil.getNodeValue;
import static sh.ball.parser.XmlUtil.getXMLDocument;

import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Function;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import javax.xml.parsers.ParserConfigurationException;

import org.unbescape.html.HtmlEscape;
import org.w3c.dom.Document;
import org.w3c.dom.Node;
import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.parser.FileParser;
import sh.ball.shapes.ShapeFrameSet;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class SvgParser extends FileParser<FrameSet<List<Shape>>> {

  private final Map<Character, Function<List<Float>, List<Shape>>> commandMap;
  private final SvgState state;
  private final InputStream input;

  private Document svg;

  public SvgParser(InputStream input) {
    this.input = input;
    this.state = new SvgState();
    this.commandMap = new HashMap<>();
    initialiseCommandMap();
  }

  public SvgParser(String path) throws FileNotFoundException {
    this(new FileInputStream(path));
    checkFileExtension(path);
  }

  // Map command chars to function calls.
  private void initialiseCommandMap() {
    commandMap.put('M', (args) -> MoveTo.absolute(state, args));
    commandMap.put('m', (args) -> MoveTo.relative(state, args));
    commandMap.put('L', (args) -> LineTo.absolute(state, args));
    commandMap.put('l', (args) -> LineTo.relative(state, args));
    commandMap.put('H', (args) -> LineTo.horizontalAbsolute(state, args));
    commandMap.put('h', (args) -> LineTo.horizontalRelative(state, args));
    commandMap.put('V', (args) -> LineTo.verticalAbsolute(state, args));
    commandMap.put('v', (args) -> LineTo.verticalRelative(state, args));
    commandMap.put('C', (args) -> CurveTo.absolute(state, args));
    commandMap.put('c', (args) -> CurveTo.relative(state, args));
    commandMap.put('S', (args) -> CurveTo.smoothAbsolute(state, args));
    commandMap.put('s', (args) -> CurveTo.smoothRelative(state, args));
    commandMap.put('Q', (args) -> CurveTo.quarticAbsolute(state, args));
    commandMap.put('q', (args) -> CurveTo.quarticRelative(state, args));
    commandMap.put('T', (args) -> CurveTo.quarticSmoothAbsolute(state, args));
    commandMap.put('t', (args) -> CurveTo.quarticSmoothRelative(state, args));
    commandMap.put('A', (args) -> EllipticalArcTo.absolute(state, args));
    commandMap.put('a', (args) -> EllipticalArcTo.relative(state, args));
    commandMap.put('Z', (args) -> ClosePath.absolute(state, args));
    commandMap.put('z', (args) -> ClosePath.relative(state, args));
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
  public String getFileExtension() {
    return "svg";
  }

  @Override
  public FrameSet<List<Shape>> parse()
    throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException {
    this.svg = getXMLDocument(input);
    List<Node> svgElem = asList(svg.getElementsByTagName("svg"));
    List<Shape> shapes = new ArrayList<>();

    if (svgElem.size() != 1) {
      throw new IllegalArgumentException("SVG has either zero or more than one svg element.");
    }

    // Get all d attributes within path elements in the SVG file.
    for (Node node : getAttributesOnTags(svg, "path", "d")) {
      shapes.addAll(parsePath(node.getNodeValue()));
    }

    return new ShapeFrameSet(Shape.normalize(shapes));
  }

  /* Given a character, will return the glyph associated with it.
   * Assumes that the .svg loaded has character glyphs. */
  public List<Shape> parseGlyphsWithUnicode(char unicode) {
    for (Node node : asList(svg.getElementsByTagName("glyph"))) {
      String unicodeString = getNodeValue(node, "unicode");

      if (unicodeString == null) {
        throw new IllegalArgumentException("Glyph should have unicode attribute.");
      }

      /* Removes all html escaped characters, allowing it to be directly compared to the unicode
       * parameter. */
      String decodedString = HtmlEscape.unescapeHtml(unicodeString);

      if (String.valueOf(unicode).equals(decodedString)) {
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

    state.currPoint = new Vector2();
    state.prevCubicControlPoint = null;
    state.prevQuadraticControlPoint = null;
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

      // Use the nums to get a list of sh.ball.shapes, using the first character in the command to specify
      // the function to use.
      svgShapes.addAll(commandMap.get(commandChar).apply(nums));

      if (!String.valueOf(commandChar).matches("[csCS]")) {
        state.prevCubicControlPoint = null;
      }
      if (!String.valueOf(commandChar).matches("[qtQT]")) {
        state.prevQuadraticControlPoint = null;
      }
    }

    return svgShapes;
  }

  public static boolean isSvgFile(String path) {
    return path.matches(".*\\.svg");
  }
}
