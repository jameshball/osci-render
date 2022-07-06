package sh.ball.gui;

@FunctionalInterface
public interface ExceptionConsumer<T> {
  void accept(T value) throws Exception;
}
