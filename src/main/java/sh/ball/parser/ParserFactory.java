package sh.ball.parser;

import sh.ball.audio.FrameSource;
import sh.ball.parser.lua.LuaParser;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.svg.SvgParser;
import sh.ball.parser.txt.TextParser;
import sh.ball.shapes.Shape;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.util.List;

public class ParserFactory {

  public static FileParser<FrameSource<List<Shape>>> getParser(String filePath, byte[] fileData) throws IOException {
    ByteArrayInputStream bais = new ByteArrayInputStream(fileData);
    if (ObjParser.isObjFile(filePath)) {
      return new ObjParser(bais);
    } else if (SvgParser.isSvgFile(filePath)) {
      return new SvgParser(bais);
    } else if (TextParser.isTxtFile(filePath)) {
      return new TextParser(bais);
    } else if (filePath.matches(".*\\.osci")) {
      throw new IOException(".osci project files should be opened using File > Open Project");
    }
    throw new IOException("Can't parse " + new File(filePath).getName());
  }

}
