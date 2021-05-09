package sh.ball.audio;

import sh.ball.audio.effect.Effect;

public interface Renderer<T> extends Runnable {

  void stop();

  void setQuality(double quality);

  void addFrame(T frame);

  void flushFrames();

  void addEffect(Object identifier, Effect effect);

  void removeEffect(Object identifier);
}
