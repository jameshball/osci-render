package sh.ball;

public interface FrameSet<T> {
  T next();
  void setFrameSettings(Object settings);
}
