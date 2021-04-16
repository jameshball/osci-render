package sh.ball.parser.obj;

import sh.ball.FrameSet;
import sh.ball.engine.Camera;
import sh.ball.engine.Vector3;
import sh.ball.engine.WorldObject;
import sh.ball.shapes.Shape;

import java.util.List;

public class ObjFrameSet implements FrameSet<List<Shape>> {

  private final WorldObject object;
  private final Camera camera;
  private final Vector3 rotation;

  public ObjFrameSet(WorldObject object, Camera camera, Vector3 rotation) {
    this.object = object;
    this.camera = camera;
    this.rotation = rotation;
  }

  @Override
  public List<Shape> next() {
    object.rotate(rotation);
    return camera.draw(object);
  }
}
