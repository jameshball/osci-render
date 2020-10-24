package parser;

import java.io.IOException;
import java.util.List;
import java.util.regex.Pattern;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import shapes.Shape;

public abstract class FileParser {

  public static String fileExtension;

  public static String getFileExtension() {
    return fileExtension;
  }

  public FileParser(String path) throws IOException, SAXException, ParserConfigurationException {
    checkFileExtension(path);
    parseFile(path);
  }

  private static void checkFileExtension(String path) throws IllegalArgumentException {
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
