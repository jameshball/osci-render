package sh.ball.audio.effect;

public enum EffectType {
  VECTOR_CANCELLING(0),
  BIT_CRUSH(1),
  ROTATE(50),
  TRANSLATE(51),
  VERTICAL_DISTORT(2),
  HORIZONTAL_DISTORT(3),
  WOBBLE(4),
  TRACE_MIN(-1),
  TRACE_MAX(-1),
  SMOOTH(100),
  DEPTH_3D(52),
  Z_POS_3D(-1),
  ROTATE_SPEED_3D(-1),
  ROTATE_X(-1),
  ROTATE_Y(-1),
  ROTATE_Z(-1),
  TRANSLATE_SPEED(-1),
  VISIBILITY(-1),
  SCALE(-1),
  FOCAL_LENGTH(-1),
  OBJ_ROTATE_X(-1),
  OBJ_ROTATE_Y(-1),
  OBJ_ROTATE_Z(-1),
  OBJ_ROTATE_SPEED(-1);

  public final int precedence;

  EffectType(int precedence) {
    this.precedence = precedence;
  }
}
