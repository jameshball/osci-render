package sh.ball.parser;

import java.io.IOException;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;

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

  public abstract T parse() throws Exception;
}
