package sh.ball.parser;

import org.xml.sax.SAXException;
import sh.ball.audio.FrameSet;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.svg.SvgParser;
import sh.ball.parser.txt.TextParser;
import sh.ball.shapes.Shape;

import javax.xml.parsers.ParserConfigurationException;
import java.io.File;
import java.io.IOException;
import java.util.List;

public class ParserFactory {

  public static FileParser<FrameSet<List<Shape>>> getParser(String filePath) throws IOException, ParserConfigurationException, SAXException {
    if (ObjParser.isObjFile(filePath)) {
      return new ObjParser(filePath);
    } else if (SvgParser.isSvgFile(filePath)) {
      return new SvgParser(filePath);
    } else if (TextParser.isTxtFile(filePath)) {
      return new TextParser(filePath);
    }
    throw new IOException("No known parser that can parse " + new File(filePath).getName());
  }

}
