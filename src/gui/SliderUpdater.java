package gui;

import java.util.function.Consumer;

public class SliderUpdater<T> {

  private final Settable<String> settable;
  private final Consumer<T> function;

  public SliderUpdater(Settable<String> settable, Consumer<T> function) {
    this.settable = settable;
    this.function = function;
  }

  public void update(T value) {
    settable.set(value.toString());
    function.accept(value);
  }
}
