package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public record ObjFrameSettings(Double focalLength, Vector3 cameraPos) {

  public ObjFrameSettings(double focalLength) {
    this(focalLength, null);
  }

  public ObjFrameSettings(Vector3 cameraPos) {
    this(null, cameraPos);
  }
}
