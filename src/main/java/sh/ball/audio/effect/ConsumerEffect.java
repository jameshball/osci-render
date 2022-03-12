package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

import java.util.function.Consumer;

public class ConsumerEffect implements SettableEffect {

  private final Consumer<Double> consumer;

  public ConsumerEffect(Consumer<Double> consumer) {
    this.consumer = consumer;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    return vector;
  }

  @Override
  public void setValue(double trace) {
    consumer.accept(trace);
  }
}
