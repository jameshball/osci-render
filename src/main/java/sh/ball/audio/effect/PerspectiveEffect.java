package sh.ball.audio.effect;

import org.luaj.vm2.LuaValue;
import sh.ball.engine.Vector3;
import sh.ball.parser.lua.LuaExecutor;
import sh.ball.shapes.Vector2;

import javax.xml.transform.Result;

// 3D rotation effect
public class PerspectiveEffect implements SettableEffect {

  private final LuaExecutor executor;

  private double zPos = 1.0;
  private Vector3 baseRotation = new Vector3(Math.PI, Math.PI, 0);
  private Vector3 currentRotation = new Vector3();
  private double rotateSpeed = 0.0;
  private double effectScale = 1.0;

  public PerspectiveEffect(LuaExecutor executor) {
    this.executor = executor;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    currentRotation = currentRotation.add(baseRotation.scale(rotateSpeed));

    Vector3 vertex = new Vector3(vector.x, vector.y, 0.0);
    vertex = vertex.rotate(baseRotation.add(currentRotation));

    executor.setVariable("x", vertex.x);
    executor.setVariable("y", vertex.y);
    executor.setVariable("z", -zPos);
    double x = vertex.x;
    double y = vertex.y;
    double z = -zPos;
    try {
      LuaValue result = executor.execute();
      x = result.get(1).checkdouble();
      y = result.get(2).checkdouble();
      z = result.get(3).checkdouble();
    } catch (Exception ignored) {}

    double focalLength = 1.0;
    return new Vector2(
      (1 - effectScale) * vector.x + effectScale * (x * focalLength / z),
      (1 - effectScale) * vector.y + effectScale * (y * focalLength / z)
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
