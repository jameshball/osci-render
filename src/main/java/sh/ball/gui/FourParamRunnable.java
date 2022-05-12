package sh.ball.gui;

@FunctionalInterface
public interface FourParamRunnable<T, U, V, W> {
  void run(T t, U u, V v, W w);
}
