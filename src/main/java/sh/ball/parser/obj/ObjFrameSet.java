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
  private final Vector3 rotation;
  private final boolean isDefaultPosition;

  public ObjFrameSet(WorldObject object, Camera camera, Vector3 rotation, boolean isDefaultPosition) {
    this.object = object;
    this.camera = camera;
    this.rotation = rotation;
    this.isDefaultPosition = isDefaultPosition;
  }

  @Override
  public List<Shape> next() {
    object.rotate(rotation);
    return camera.draw(object);
  }

  @Override
  public void setFrameSettings(Object settings) {
    if (settings instanceof ObjFrameSettings obj) {
      Double focalLength = obj.focalLength();
      Vector3 cameraPos = obj.cameraPos();

      if (focalLength != null && camera.getFocalLength() != focalLength) {
        camera.setFocalLength(focalLength);
        if (isDefaultPosition) {
          camera.findZPos(object);
        }
      }
      if (cameraPos != null && camera.getPos() != cameraPos) {
        camera.setPos(cameraPos);
      }
    }
  }
}
