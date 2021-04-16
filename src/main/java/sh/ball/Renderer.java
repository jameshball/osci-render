package sh.ball;

public interface Renderer<T> extends Runnable {
  void stop();
  void setQuality(double quality);
  void addFrame(T frame);
}
