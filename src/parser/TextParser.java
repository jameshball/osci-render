package parser;

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
import shapes.Shape;
import shapes.Vector2;

public class TextParser extends FileParser{

  private static final char WIDE_CHAR = 'W';
  private static final double HEIGHT_SCALAR = 1.6;

  private final Map<Character, List<Shape>> charToShape;
  private final List<String> text;

  private List<Shape> shapes;

  public TextParser(String path, String font)
      throws IOException, SAXException, ParserConfigurationException {
    this.charToShape = new HashMap<>();
    this.shapes = new ArrayList<>();
    this.text = Files.readAllLines(Paths.get(path), Charset.defaultCharset());
    parseFile(font);
  }

  @Override
  protected String getFileExtension() {
    return ".txt";
  }

  @Override
  protected void parseFile(String path)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException {
    SvgParser parser = new SvgParser(path);

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

    for (int i = 0, textSize = text.size(); i < textSize; i++) {
      String line = text.get(i);
      char[] lineChars = line.toCharArray();
      for (int j = 0; j < lineChars.length; j++) {
        shapes.addAll(Shape.translate(charToShape.get(lineChars[j]), new Vector2(j * width, -i * height)));
      }
    }

    shapes = Shape.flip(Shape.normalize(shapes));
  }

  @Override
  public List<Shape> nextFrame() {
    return shapes;
  }
}
