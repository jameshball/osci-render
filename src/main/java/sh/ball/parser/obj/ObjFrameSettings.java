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
  public Double rotateX = null;
  public Double rotateY = null;
  public Double rotateZ = null;
  public Double actualRotateX = null;
  public Double actualRotateY = null;
  public Double actualRotateZ = null;

  protected ObjFrameSettings() {}

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

  protected ObjFrameSettings withRotateX(double x) {
    this.rotateX = x;
    return this;
  }

  protected ObjFrameSettings withRotateY(double y) {
    this.rotateY = y;
    return this;
  }

  protected ObjFrameSettings withRotateZ(double z) {
    this.rotateZ = z;
    return this;
  }

  protected ObjFrameSettings withActualRotateX(double x) {
    this.actualRotateX = x;
    return this;
  }

  protected ObjFrameSettings withActualRotateY(double y) {
    this.actualRotateY = y;
    return this;
  }

  protected ObjFrameSettings withActualRotateZ(double z) {
    this.actualRotateZ = z;
    return this;
  }
}
