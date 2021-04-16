package sh.ball.parser.txt;

import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.parser.FileParser;
import sh.ball.parser.ShapeFrameSet;
import sh.ball.parser.svg.SvgParser;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class TextParser extends FileParser<FrameSet<List<Shape>>> {

  private static final char WIDE_CHAR = 'W';
  private static final double HEIGHT_SCALAR = 1.6;
  private static final String DEFAULT_FONT = TextParser.class.getResource("/fonts/SourceCodePro-ExtraLight.svg").getPath();

  private final Map<Character, List<Shape>> charToShape;
  private final String filePath;
  private final String fontPath;

  public TextParser(String path, String font) {
    checkFileExtension(path);
    this.filePath = path;
    this.fontPath = font;
    this.charToShape = new HashMap<>();
  }

  public TextParser(String path) {
    this(path, DEFAULT_FONT);
  }

  @Override
  public String getFileExtension() {
    return "txt";
  }

  @Override
  public FrameSet<List<Shape>> parse() throws IllegalArgumentException, IOException, ParserConfigurationException, SAXException {
    List<String> text = Files.readAllLines(Paths.get(filePath), Charset.defaultCharset());
    SvgParser parser = new SvgParser(fontPath);
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

  @Override
  public String getFilePath() {
    return filePath;
  }

  public static boolean isTxtFile(String path) {
    return path.matches(".*\\.txt");
  }
}
