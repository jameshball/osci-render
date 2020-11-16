package audio;

import engine.Camera;
import java.io.IOException;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import parser.FileParser;
import parser.ObjParser;
import parser.SvgParser;
import parser.TextParser;

// Helper class for AudioClient that deals with optional program arguments.
final class AudioArgs {

  private final String fontPath;
  private final float[] optionalArgs;

  final String filePath;

  AudioArgs(String[] args) throws IllegalAudioArgumentException {
    if (args.length < 1 || args.length > 6) {
      throw new IllegalAudioArgumentException();
    }

    filePath = args[0];
    optionalArgs = new float[args.length - 1];
    if (filePath.matches(".*\\.txt")) {
      fontPath = args[1];
      return;
    }
    fontPath = "";

    for (int i = 0; i < optionalArgs.length; i++) {
      optionalArgs[i] = Float.parseFloat(args[i + 1]);
    }
  }

  FileParser getFileParser() throws IOException, ParserConfigurationException, SAXException {
    if (filePath.matches(".*\\.obj")) {
      return new ObjParser(filePath, rotateSpeed(), cameraX(), cameraY(), cameraZ(), focalLength(),
          isDefaultPosition());
    } else if (filePath.matches(".*\\.svg")) {
      return new SvgParser(filePath);
    } else if (filePath.matches(".*\\.txt")) {
      return new TextParser(filePath, fontPath);
    } else {
      throw new IllegalArgumentException(
          "Provided file extension in file " + filePath + " not supported.");
    }
  }

  float rotateSpeed() {
    return getArg(0, 0);
  }

  float focalLength() {
    return getArg(1, (float) Camera.DEFAULT_FOCAL_LENGTH);
  }

  float cameraX() {
    return getArg(2, 0);
  }

  float cameraY() {
    return getArg(3, 0);
  }

  float cameraZ() {
    return getArg(4, 0);
  }

  boolean isDefaultPosition() {
    return optionalArgs.length <= 2;
  }

  private float getArg(int n, float defaultVal) {
    return optionalArgs.length > n ? optionalArgs[n] : defaultVal;
  }

  private static class IllegalAudioArgumentException extends IllegalArgumentException {

    private static final String USAGE = "Incorrect usage.\nUsage: osci-render objFilePath "
        + "[rotateSpeed] [focalLength] [cameraX] [cameraY] [cameraZ]";

    public IllegalAudioArgumentException() {
      super(USAGE);
    }
  }
}
