package sh.ball.engine;

import sh.ball.audio.FrameSource;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.List;
import java.util.Objects;

public class ObjectSet implements FrameSource<List<Shape>> {

  private final CameraDrawKernel kernel = new CameraDrawKernel();

  public List<WorldObject> objects;
  public List<float[]> objectMatrices;
  public List<Vector3[]> pathObjects;
  public List<float[]> pathMatrices;
  private boolean active = true;
  private float focalLength;

  public ObjectSet() {}

  public synchronized void setObjects(List<WorldObject> objects, List<float[]> matrices, List<Vector3[]> pathObjects, List<float[]> pathMatrices, float focalLength) {
    this.objects = objects;
    this.objectMatrices = matrices;
    this.pathObjects = pathObjects;
    this.pathMatrices = pathMatrices;
    this.focalLength = focalLength;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;

    ObjectSet objectSet = (ObjectSet) o;

    if (!Objects.equals(objects, objectSet.objects))
      return false;
    return Objects.equals(objectMatrices, objectSet.objectMatrices);
  }

  @Override
  public int hashCode() {
    int result = objects != null ? objects.hashCode() : 0;
    result = 31 * result + (objectMatrices != null ? objectMatrices.hashCode() : 0);
    return result;
  }

  @Override
  public synchronized List<Shape> next() {
    if ((objects == null || objectMatrices == null || objects.isEmpty() || objectMatrices.isEmpty())
        && (pathObjects == null || pathMatrices == null || pathObjects.isEmpty() || pathMatrices.isEmpty())) {
      System.out.println("nothing to draw!");
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
