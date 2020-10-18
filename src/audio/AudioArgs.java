package audio;

import engine.Camera;

// Helper class for AudioClient that deals with optional program arguments.
final class AudioArgs {

  final String objFilePath;
  final float[] optionalArgs;

  AudioArgs(String[] args) throws IllegalAudioArgumentException {
    if (args.length < 1 || args.length > 6) {
      throw new IllegalAudioArgumentException();
    }

    objFilePath = args[0];
    optionalArgs = new float[args.length - 1];

    for (int i = 0; i < optionalArgs.length; i++) {
      optionalArgs[i] = Float.parseFloat(args[i + 1]);
    }
  }

  String objFilePath() {
    return objFilePath;
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

  private class IllegalAudioArgumentException extends IllegalArgumentException {

    private static final String USAGE = "Incorrect usage.\nUsage: osci-render objFilePath "
        + "[rotateSpeed] [cameraX] [cameraY] [cameraZ] [focalLength]";

    public IllegalAudioArgumentException() {
      super(USAGE);
    }
  }
}
