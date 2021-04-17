package sh.ball;

public interface MovableRenderer<S, T> extends Renderer<S> {

  void setRotationSpeed(double speed);

  void setTranslation(T translation);

  void setTranslationSpeed(double speed);

  void setScale(double scale);
}
