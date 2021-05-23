package sh.ball.audio;

import sh.ball.parser.obj.Listener;

public interface FrameSet<T> {

  T next();

  void setFrameSettings(Object settings);

  void addListener(Listener listener);
}
