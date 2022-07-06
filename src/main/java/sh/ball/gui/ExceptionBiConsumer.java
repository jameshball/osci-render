package sh.ball.gui;

@FunctionalInterface
public interface ExceptionBiConsumer<T, S> {
  void accept(T value1, S value2) throws Exception;
}
