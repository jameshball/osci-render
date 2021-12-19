package sh.ball.shapes;

import sh.ball.audio.FrameSource;

import java.util.List;

public class ShapeFrameSource implements FrameSource<List<Shape>> {

  private final List<Shape> shapes;

  private boolean active = true;

  public ShapeFrameSource(List<Shape> shapes) {
    this.shapes = shapes;
  }

  @Override
  public List<Shape> next() {
    return shapes;
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
  public void setFrameSettings(Object settings) {}

  @Override
  public Object getFrameSettings() { return null; }
}
