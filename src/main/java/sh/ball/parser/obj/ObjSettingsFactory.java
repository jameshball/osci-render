package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public class ObjSettingsFactory {

  public static ObjFrameSettings focalLength(double focalLength) {
    return new ObjFrameSettings(focalLength);
  }

  public static ObjFrameSettings cameraPosition(Vector3 cameraPos) {
    return new ObjFrameSettings(cameraPos);
  }

  public static ObjFrameSettings baseRotation(Vector3 baseRotation) {
    return new ObjFrameSettings(baseRotation, null);
  }

  public static ObjFrameSettings rotation(Vector3 baseRotation, Vector3 currentRotation) {
    return new ObjFrameSettings(null, null, baseRotation, currentRotation, null, false, null, null);
  }

  public static ObjFrameSettings rotateSpeed(double rotateSpeed) {
    return new ObjFrameSettings(null, rotateSpeed);
  }

  public static ObjFrameSettings resetRotation() {
    return new ObjFrameSettings(true);
  }

  public static ObjFrameSettings hideEdges(Boolean hideEdges) {
    return new ObjFrameSettings(null, null, null, null, null, false, hideEdges, null);
  }

  public static ObjFrameSettings renderUsingGpu(Boolean usingGpu) {
    return new ObjFrameSettings(null, null, null, null, null, false, null, usingGpu);
  }
}
