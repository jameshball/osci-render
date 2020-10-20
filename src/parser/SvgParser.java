package parser;

import java.util.List;
import shapes.Shape;

public class SvgParser implements FileParser {

  @Override
  public String getFileExtension() {
    return ".svg";
  }

  @Override
  public List<? extends Shape> parseFile(String path) {
    return null;
  }
}
