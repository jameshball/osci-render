package sh.ball.audio.effect;

public enum EffectType {
  VECTOR_CANCELLING(0),
  BIT_CRUSH(1),
  ROTATE(50),
  TRANSLATE(51),
  VERTICAL_DISTORT(2),
  HORIZONTAL_DISTORT(3),
  WOBBLE(4),
  TRACE_MIN(-5),
  TRACE_MAX(-4),
  SMOOTH(100),
  ROTATE_3D(52),
  TRANSLATE_SPEED(-3),
  VISIBILITY(-2),
  SCALE(-1);

  public final int precedence;

  EffectType(int precedence) {
    this.precedence = precedence;
  }
}
