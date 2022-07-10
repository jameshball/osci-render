package sh.ball.audio.effect;

import sh.ball.engine.Vector3;
import sh.ball.shapes.Vector2;

// 3D rotation effect
public class PerspectiveEffect implements SettableEffect {

  private double zPos = 1.0;
  private Vector3 baseRotation = new Vector3(Math.PI, Math.PI, 0);
  private Vector3 currentRotation = new Vector3();
  private double rotateSpeed = 0.0;
  private double effectScale = 1.0;

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    currentRotation = currentRotation.add(baseRotation.scale(rotateSpeed));

    Vector3 vertex = new Vector3(vector.x, vector.y, 0.0);
    vertex = vertex.rotate(baseRotation.add(currentRotation));

    double focalLength = 1.0;
    return new Vector2(
      (1 - effectScale) * vector.x + effectScale * (vertex.x * focalLength / (vertex.z - zPos)),
      (1 - effectScale) * vector.y + effectScale * (vertex.y * focalLength / (vertex.z - zPos))
    );
  }

  @Override
  public void setValue(double value) {
    this.effectScale = value;
  }

  public void setZPos(double value) {
    zPos = 1.0 + (value - 0.1) * 3;
  }

  public void setRotateSpeed(double rotateSpeed) {
    this.rotateSpeed = linearSpeedToActualSpeed(rotateSpeed);
  }

  private double linearSpeedToActualSpeed(double rotateSpeed) {
    return (Math.exp(3 * Math.min(10, Math.abs(rotateSpeed))) - 1) / 50000;
  }

  public void setRotationX(double rotateX) {
    this.baseRotation = new Vector3(rotateX * Math.PI, baseRotation.y, baseRotation.z);
  }

  public void setRotationY(double rotateY) {
    this.baseRotation = new Vector3(baseRotation.x, rotateY * Math.PI, baseRotation.z);
  }

  public void setRotationZ(double rotateZ) {
    this.baseRotation = new Vector3(baseRotation.x, baseRotation.y, rotateZ * Math.PI);
  }

  public void resetRotation() {
    baseRotation = new Vector3();
    currentRotation = new Vector3(Math.PI, Math.PI, 0);
  }
}
