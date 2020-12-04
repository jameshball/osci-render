package sh.ball.gui;

@FunctionalInterface
public interface Settable<T> {

  void set(T value);
}
