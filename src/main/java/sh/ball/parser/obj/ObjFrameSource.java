package sh.ball.parser.obj;

import sh.ball.audio.FrameSource;
import sh.ball.engine.Camera;
import sh.ball.engine.Vector3;
import sh.ball.engine.WorldObject;
import sh.ball.shapes.Shape;

import java.util.List;

public class ObjFrameSource implements FrameSource<List<Shape>> {

  private final WorldObject object;
  private final Camera camera;

  private Vector3 baseRotation = new Vector3(Math.PI, Math.PI, 0);
  private Vector3 currentRotation = new Vector3();
  private Double rotateSpeed = 0.0;
  private boolean active = true;

  public ObjFrameSource(WorldObject object, Camera camera) {
    this.object = object;
    this.camera = camera;
  }

  @Override
  public List<Shape> next() {
    currentRotation = currentRotation.add(baseRotation.scale(rotateSpeed));
    object.setRotation(baseRotation.add(currentRotation));
    return camera.draw(object);
  }

  @Override
  public boolean isActive() {
    return active;
  }

  @Override
  public void disable() {
    active = false;
  }

  @Override
  public void enable() {
    active = true;
  }

  // TODO: Refactor!
  @Override
  public void setFrameSettings(Object settings) {
    if (settings instanceof ObjFrameSettings obj) {
      if (obj.focalLength != null && camera.getFocalLength() != obj.focalLength) {
        camera.setFocalLength(obj.focalLength);
      }
      if (obj.cameraPos != null && camera.getPos() != obj.cameraPos) {
        camera.setPos(obj.cameraPos);
      }
      if (obj.baseRotation != null) {
        this.baseRotation = obj.baseRotation;
      }
      if (obj.currentRotation != null) {
        this.currentRotation = obj.currentRotation;
      }
      if (obj.rotateSpeed != null) {
        this.rotateSpeed = obj.rotateSpeed;
      }
      if (obj.resetRotation) {
        object.resetRotation();
      }
      if (obj.hideEdges != null) {
        camera.hideEdges(obj.hideEdges);
      }
      if (obj.usingGpu != null) {
        camera.usingGpu(obj.usingGpu);
      }
      if (obj.rotateX != null) {
        this.baseRotation = new Vector3(obj.rotateX, baseRotation.y, baseRotation.z);
      }
      if (obj.rotateY != null) {
        this.baseRotation = new Vector3(baseRotation.x, obj.rotateY, baseRotation.z);
      }
      if (obj.rotateZ != null) {
        this.baseRotation = new Vector3(baseRotation.x, baseRotation.y, obj.rotateZ);
      }
      if (obj.actualRotateX != null) {
        this.baseRotation = new Vector3(0, baseRotation.y, baseRotation.z);
        this.currentRotation = new Vector3(obj.actualRotateX, currentRotation.y, currentRotation.z);
      }
      if (obj.actualRotateY != null) {
        this.baseRotation = new Vector3(baseRotation.x, 0, baseRotation.z);
        this.currentRotation = new Vector3(currentRotation.x, obj.actualRotateY, currentRotation.z);
      }
      if (obj.actualRotateZ != null) {
        this.baseRotation = new Vector3(baseRotation.x, baseRotation.y, 0);
        this.currentRotation = new Vector3(currentRotation.x, currentRotation.y, obj.actualRotateZ);
      }
    }
  }

  // TODO: Refactor!
  @Override
  public Object getFrameSettings() {
    return new ObjFrameSettings(null, null, baseRotation, currentRotation, null, false, camera.edgesHidden(), camera.isUsingGpu());
  }
}
