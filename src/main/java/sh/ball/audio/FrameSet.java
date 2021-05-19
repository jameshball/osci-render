package sh.ball.audio;

public interface FrameSet<T> {

  T next();

  void setFrameSettings(Object settings);
}
