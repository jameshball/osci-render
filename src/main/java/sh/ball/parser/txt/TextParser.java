package sh.ball.parser.txt;

import java.awt.*;
import java.awt.font.FontRenderContext;
import java.awt.font.GlyphVector;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.audio.FrameSource;
import sh.ball.parser.FileParser;
import sh.ball.shapes.ShapeFrameSource;
import sh.ball.shapes.Shape;

public class TextParser extends FileParser<FrameSource<List<Shape>>> {

  private final InputStream input;
  private final String familyName;
  private final int style;
  Pattern textSizePattern = Pattern.compile("^\\$text_size=(\\d+)");
  Pattern escapePattern = Pattern.compile("\\\\(.)");

  public TextParser(InputStream input, String familyName, int style) {
    this.input = input;
    this.familyName = familyName;
    this.style = style;
  }

  @Override
  public String getFileExtension() {
    return "txt";
  }

  public double getLineHeight(Font font, FontRenderContext frc) {
    return font.getMaxCharBounds(frc).getHeight();
  }

  @Override
  public FrameSource<List<Shape>> parse() throws IllegalArgumentException, IOException, ParserConfigurationException, SAXException {
    String text = new String(input.readAllBytes(), StandardCharsets.UTF_8);
    Font font = new Font(familyName, style, 100);
    FontRenderContext frc = new FontRenderContext(null, false, false);

    List<Shape> shapes = new ArrayList<>();
    float yOffset = 0;
    for (String line : text.split("\n")) {
      Matcher matcher = textSizePattern.matcher(line);

      if (matcher.find()) {
        float size = Float.parseFloat(matcher.group(1));
        line = matcher.replaceFirst("");
        font = font.deriveFont(size);
      } else {
        font = font.deriveFont((float) 100);
      }

      matcher = escapePattern.matcher(line);

      line = matcher.replaceAll("$1");

      GlyphVector glyphVector = font.createGlyphVector(frc, line);
      yOffset += getLineHeight(font, frc);
      shapes.addAll(Shape.convert(glyphVector.getOutline(0, yOffset)));
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
