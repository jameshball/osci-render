package parser;

import java.util.List;
import shapes.Shape;

public interface FileParser {

  String getFileExtension();

  List<? extends Shape> parseFile(String path);
}
