package sh.ball.audio;

import sh.ball.shapes.Vector2;

public interface Effect {
  Vector2 apply(int count, Vector2 vector);
}
