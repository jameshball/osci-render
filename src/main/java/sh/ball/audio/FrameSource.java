package sh.ball.audio;

public interface FrameSource<T> {

  T next();

  boolean isActive();

  void disable();

  void enable();

  void setFrameSettings(Object settings);
}
