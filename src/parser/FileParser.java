package parser;

import java.io.IOException;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import shapes.Shape;

public abstract class FileParser {

  protected abstract String getFileExtension();

  protected void checkFileExtension(String path) throws IllegalArgumentException {
    if (!hasCorrectFileExtension(path)) {
      throw new IllegalArgumentException(
          "File to parse is not a ." + getFileExtension() + " file.");
    }
  }

  public boolean hasCorrectFileExtension(String path) {
    return path.matches(".*\\." + getFileExtension());
  }

  protected abstract void parseFile(String path)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException;

  public abstract List<Shape> nextFrame();
  public abstract String getFilePath();
}
