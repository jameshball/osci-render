package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public class ObjFrameSettings {

  protected Double focalLength;
  protected Vector3 cameraPos;
  protected Vector3 rotation;
  protected Double rotateSpeed;
  protected boolean resetRotation = false;

  protected ObjFrameSettings(double focalLength) {
    this.focalLength = focalLength;
  }

  protected ObjFrameSettings(Vector3 cameraPos) {
    this.cameraPos = cameraPos;
  }

  protected ObjFrameSettings(Vector3 rotation, Double rotateSpeed) {
    this.rotation = rotation;
    this.rotateSpeed = rotateSpeed;
  }

  protected ObjFrameSettings(boolean resetRotation) {
    this.resetRotation = resetRotation;
  }
}
