package sh.ball.audio.engine;

public interface AudioInput extends Runnable {
  void addListener(AudioInputListener listener);

  void run();

  void stop();

  boolean isAvailable();
}
