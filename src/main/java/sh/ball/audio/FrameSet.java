package sh.ball.audio;

public interface FrameSet<T> {

  T next();

  Object setFrameSettings(Object settings);
}
