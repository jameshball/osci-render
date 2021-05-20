package sh.ball.parser.obj;

import sh.ball.audio.FrameSet;
import sh.ball.engine.Camera;
import sh.ball.engine.Vector3;
import sh.ball.engine.WorldObject;
import sh.ball.shapes.Shape;

import java.util.List;

public class ObjFrameSet implements FrameSet<List<Shape>> {

  private final WorldObject object;
  private final Camera camera;

  private Vector3 rotation = new Vector3();
  private Double rotateSpeed = 0.0;

  public ObjFrameSet(WorldObject object, Camera camera) {
    this.object = object;
    this.camera = camera;
  }

  @Override
  public List<Shape> next() {
    if (rotateSpeed == 0) {
      object.setRotation(rotation);
    } else {
      object.rotate(rotation.scale(rotateSpeed));
    }
    return camera.draw(object);
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
      if (obj.rotation != null) {
        this.rotation = obj.rotation;
      }
      if (obj.rotateSpeed != null) {
        this.rotateSpeed = obj.rotateSpeed;
      }
      if (obj.resetRotation) {
        object.resetRotation();
      }
    }
  }
}
