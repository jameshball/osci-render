package sh.ball.parser.obj;

import sh.ball.engine.Vector3;

public class ObjFrameSettings {

  public Double focalLength;
  public Vector3 cameraPos;
  public Vector3 baseRotation;
  public Vector3 currentRotation;
  public Double rotateSpeed;
  public boolean resetRotation = false;
  public Boolean hideEdges = null;
  public Boolean usingGpu = null;

  protected ObjFrameSettings(Double focalLength, Vector3 cameraPos, Vector3 baseRotation, Vector3 currentRotation, Double rotateSpeed, boolean resetRotation, Boolean hideEdges, Boolean usingGpu) {
    this.focalLength = focalLength;
    this.cameraPos = cameraPos;
    this.baseRotation = baseRotation;
    this.currentRotation = currentRotation;
    this.rotateSpeed = rotateSpeed;
    this.resetRotation = resetRotation;
    this.hideEdges = hideEdges;
    this.usingGpu = usingGpu;
  }

  protected ObjFrameSettings(double focalLength) {
    this.focalLength = focalLength;
  }

  protected ObjFrameSettings(Vector3 cameraPos) {
    this.cameraPos = cameraPos;
  }

  protected ObjFrameSettings(Vector3 baseRotation, Double rotateSpeed) {
    this.baseRotation = baseRotation;
    this.rotateSpeed = rotateSpeed;
  }

  protected ObjFrameSettings(boolean resetRotation) {
    this.resetRotation = resetRotation;
  }
}
