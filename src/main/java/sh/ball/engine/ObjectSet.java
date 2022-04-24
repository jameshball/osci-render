package sh.ball.engine;

import sh.ball.audio.FrameSource;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.Collection;
import java.util.List;
import java.util.Objects;

public class ObjectSet implements FrameSource<List<Shape>> {

  private final CameraDrawKernel kernel = new CameraDrawKernel();

  public Collection<WorldObject> objects;
  public Collection<float[]> cameraSpaceMatrices;
  private boolean active = true;
  private float focalLength;

  public ObjectSet() {}

  public void setObjects(Collection<WorldObject> objects, Collection<float[]> matrices, float focalLength) {
    this.objects = objects;
    this.cameraSpaceMatrices = matrices;
    this.focalLength = focalLength;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;

    ObjectSet objectSet = (ObjectSet) o;

    if (!Objects.equals(objects, objectSet.objects))
      return false;
    return Objects.equals(cameraSpaceMatrices, objectSet.cameraSpaceMatrices);
  }

  @Override
  public int hashCode() {
    int result = objects != null ? objects.hashCode() : 0;
    result = 31 * result + (cameraSpaceMatrices != null ? cameraSpaceMatrices.hashCode() : 0);
    return result;
  }

  @Override
  public List<Shape> next() {
    if (objects == null || cameraSpaceMatrices == null) {
      return List.of(new Vector2());
    }
    return kernel.draw(this, focalLength);
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

  @Override
  public void setFrameSettings(Object settings) {

  }

  @Override
  public Object getFrameSettings() {
    return null;
  }
}
