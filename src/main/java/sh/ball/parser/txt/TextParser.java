package sh.ball.parser.txt;

import java.awt.*;
import java.awt.font.FontRenderContext;
import java.awt.font.GlyphVector;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.audio.FrameSource;
import sh.ball.parser.FileParser;
import sh.ball.shapes.ShapeFrameSource;
import sh.ball.shapes.Shape;

public class TextParser extends FileParser<FrameSource<List<Shape>>> {

  private static final char WIDE_CHAR = 'W';
  private static final double HEIGHT_SCALAR = 1.6;
  private final InputStream input;
  private final String familyName;
  private final int style;

  public TextParser(InputStream input, String familyName, int style) {
    this.input = input;
    this.familyName = familyName;
    this.style = style;
  }

  @Override
  public String getFileExtension() {
    return "txt";
  }

  @Override
  public FrameSource<List<Shape>> parse() throws IllegalArgumentException, IOException, ParserConfigurationException, SAXException {
    String text = new String(input.readAllBytes(), StandardCharsets.UTF_8);
    Font font = new Font(familyName, style, 100);
    FontRenderContext frc = new FontRenderContext(null, false, false);
    GlyphVector tallChar = font.createGlyphVector(frc, String.valueOf(WIDE_CHAR));
    double lineHeight = tallChar.getPixelBounds(frc, 0, 0).getHeight();

    List<Shape> shapes = new ArrayList<>();
    float yOffset = 0;
    for (String line : text.split("\n")) {
      GlyphVector glyphVector = font.createGlyphVector(frc, line);
      shapes.addAll(Shape.convert(glyphVector.getOutline(0, yOffset)));
      yOffset += lineHeight;
    }

    return new ShapeFrameSource((Shape.normalize(shapes)));
  }

  @Override
  public FrameSource<List<Shape>> get() throws Exception {
    return parse();
  }

  public static boolean isTxtFile(String path) {
    return path.matches(".*\\.txt");
  }
}
