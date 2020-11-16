package parser;

import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.file.Files;
import java.nio.file.Paths;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import shapes.Shape;

public class TextParser extends FileParser{

  private final Map<Character, List<Shape>> charToShape;

  private final List<String> text;

  public TextParser(String path, String font)
      throws IOException, SAXException, ParserConfigurationException {
    this.charToShape = new HashMap<>();
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

    for (String line : text) {
      for (char c : line.toCharArray()) {
        if (!charToShape.containsKey(c)) {
          List<Shape> glyph = parser.parseGlyphsWithUnicode(c);
          charToShape.put(c, Shape.flipShapes(Shape.normalize(glyph)));
        }
      }
    }
  }

  @Override
  public List<Shape> nextFrame() {
    return charToShape.get(text.get(0).charAt(0));
  }
}
