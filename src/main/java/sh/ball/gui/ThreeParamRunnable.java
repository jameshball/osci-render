package sh.ball.gui;

@FunctionalInterface
public interface ThreeParamRunnable<T, U, V> {
  void run(T t, U u, V v);
}
