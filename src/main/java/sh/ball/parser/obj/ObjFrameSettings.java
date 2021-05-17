package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public class ObjFrameSettings {

  protected Double focalLength;
  protected Vector3 cameraPos;
  protected Vector3 rotation;
  protected Double rotateSpeed;

  public ObjFrameSettings(double focalLength) {
    this.focalLength = focalLength;
  }

  public ObjFrameSettings(Vector3 cameraPos) {
    this.cameraPos = cameraPos;
  }

  public ObjFrameSettings(Vector3 rotation, Double rotateSpeed) {
    this.rotation = rotation;
    this.rotateSpeed = rotateSpeed;
  }
}
