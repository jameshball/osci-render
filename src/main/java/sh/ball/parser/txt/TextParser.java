package sh.ball.parser.txt;

import java.io.*;
import java.nio.charset.Charset;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.stream.Collectors;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.parser.FileParser;
import sh.ball.shapes.ShapeFrameSet;
import sh.ball.parser.svg.SvgParser;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class TextParser extends FileParser<FrameSet<List<Shape>>> {

  private static final char WIDE_CHAR = 'W';
  private static final double HEIGHT_SCALAR = 1.6;
  private static final InputStream DEFAULT_FONT = TextParser.class.getResourceAsStream("/fonts/SourceCodePro-ExtraLight.svg");

  private final Map<Character, List<Shape>> charToShape;
  private final InputStream input;
  private final InputStream font;

  public TextParser(InputStream input, InputStream font) {
    this.input = input;
    this.font = font;
    this.charToShape = new HashMap<>();
  }

  public TextParser(String path) throws FileNotFoundException {
    this(new FileInputStream(path), DEFAULT_FONT);
    checkFileExtension(path);
  }

  @Override
  public String getFileExtension() {
    return "txt";
  }

  @Override
  public FrameSet<List<Shape>> parse() throws IllegalArgumentException, IOException, ParserConfigurationException, SAXException {
    List<String> text = new BufferedReader(new InputStreamReader(input, Charset.defaultCharset())).lines().collect(Collectors.toList());
    SvgParser parser = new SvgParser(font);
    parser.parse();
    List<Shape> shapes = new ArrayList<>();

    /* WIDE_CHAR used as an example character that will be wide in most languages.
     * This helps determine the correct character width for the font chosen. */
    charToShape.put(WIDE_CHAR, parser.parseGlyphsWithUnicode(WIDE_CHAR));

    for (String line : text) {
      for (char c : line.toCharArray()) {
        if (!charToShape.containsKey(c)) {
          List<Shape> glyph = parser.parseGlyphsWithUnicode(c);
          charToShape.put(c, glyph);
        }
      }
    }

    double width = Shape.width(charToShape.get(WIDE_CHAR));
    double height = HEIGHT_SCALAR * Shape.height(charToShape.get(WIDE_CHAR));

    for (int i = 0; i < text.size(); i++) {
      char[] lineChars = text.get(i).toCharArray();
      for (int j = 0; j < lineChars.length; j++) {
        shapes.addAll(Shape.translate(
          charToShape.get(lineChars[j]),
          new Vector2(j * width, -i * height)
        ));
      }
    }

    return new ShapeFrameSet(Shape.flip(Shape.normalize(shapes)));
  }

  public static boolean isTxtFile(String path) {
    return path.matches(".*\\.txt");
  }
}
