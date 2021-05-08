package sh.ball.audio;

public interface Renderer<T> extends Runnable {

  void stop();

  void setQuality(double quality);

  void addFrame(T frame);

  void addEffect(Object identifier, Effect effect);

  void removeEffect(Object identifier);
}
