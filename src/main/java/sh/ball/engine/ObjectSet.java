package sh.ball.engine;

import sh.ball.audio.FrameSource;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.util.List;

public class ObjectSet implements FrameSource<List<Shape>> {

  private final CameraDrawKernel kernel = new CameraDrawKernel();

  public List<Vector3[][]> paths;
  public List<float[]> matrices;
  private boolean active = true;
  private float focalLength;

  public ObjectSet() {}

  public synchronized void setObjects(List<Vector3[][]> paths, List<float[]> matrices, float focalLength) {
    this.paths = paths;
    this.matrices = matrices;
    this.focalLength = focalLength;
  }

  @Override
  public synchronized List<Shape> next() {
    if (paths == null || matrices == null || paths.isEmpty() || matrices.isEmpty()) {
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
