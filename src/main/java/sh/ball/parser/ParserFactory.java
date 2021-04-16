package sh.ball.parser;

import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.svg.SvgParser;
import sh.ball.parser.txt.TextParser;
import sh.ball.shapes.Shape;

import javax.xml.parsers.ParserConfigurationException;
import java.io.IOException;
import java.util.List;
import java.util.Optional;

public class ParserFactory {

  public static Optional<FileParser<FrameSet<List<Shape>>>> getParser(String filePath) throws IOException, ParserConfigurationException, SAXException {
    if (ObjParser.isObjFile(filePath)) {
      return Optional.of(new ObjParser(filePath, 1));
    } else if (SvgParser.isSvgFile(filePath)) {
      return Optional.of(new SvgParser(filePath));
    } else if (TextParser.isTxtFile(filePath)) {
      return Optional.of(new TextParser(filePath));
    }
    return Optional.empty();
  }

}
