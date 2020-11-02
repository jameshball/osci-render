package parser;

import java.io.IOException;
import java.util.List;
import java.util.regex.Pattern;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import shapes.Shape;

public abstract class FileParser {

  public abstract String getFileExtension();

  protected void checkFileExtension(String path) throws IllegalArgumentException {
    Pattern pattern = Pattern.compile("\\." + getFileExtension() + "$");
    if (!pattern.matcher(path).find()) {
      throw new IllegalArgumentException(
          "File to parse is not a ." + getFileExtension() + " file.");
    }
  }

  protected abstract void parseFile(String path)
      throws ParserConfigurationException, IOException, SAXException, IllegalArgumentException;

  public abstract List<Shape> getShapes();
}
