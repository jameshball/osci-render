package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public class ObjSettingsFactory {

  public static ObjFrameSettings focalLength(double focalLength) {
    return new ObjFrameSettings(focalLength);
  }

  public static ObjFrameSettings cameraPosition(Vector3 cameraPos) {
    return new ObjFrameSettings(cameraPos);
  }

  public static ObjFrameSettings rotation(Vector3 rotation) {
    return new ObjFrameSettings(rotation, null);
  }

  public static ObjFrameSettings rotateSpeed(double rotateSpeed) {
    return new ObjFrameSettings(null, rotateSpeed);
  }

  public static ObjFrameSettings resetRotation() {
    return new ObjFrameSettings(true);
  }
}
