package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

// The Effect interface is intended for audio effects, but can be used for any
// effect that manipulates a Vector2 overtime. Audio effects iterate their
// frame count, which can result in different results according to the count,
// along with the vector can apply an audio effect.
public interface Effect {
  Vector2 apply(int count, Vector2 vector);
}
