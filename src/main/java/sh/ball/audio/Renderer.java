package sh.ball.audio;

import sh.ball.audio.effect.Effect;

public interface Renderer<S, T> extends Runnable {

  void stop();

  void setQuality(double quality);

  void addFrame(S frame);

  void flushFrames();

  void addEffect(Object identifier, Effect effect);

  void removeEffect(Object identifier);

  void startRecord();

  T stopRecord();
}
