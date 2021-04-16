package sh.ball.parser;

import java.io.IOException;
import java.util.List;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import sh.ball.shapes.Shape;

public abstract class FileParser<T> {

  public abstract String getFileExtension();

  protected void checkFileExtension(String path) throws IllegalArgumentException {
    if (!hasCorrectFileExtension(path)) {
      throw new IllegalArgumentException(
          "File to parse is not a ." + getFileExtension() + " file.");
    }
  }

  public boolean hasCorrectFileExtension(String path) {
    return path.matches(".*\\." + getFileExtension());
  }

  public abstract T parse()
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException;

  public abstract String getFilePath();
}
