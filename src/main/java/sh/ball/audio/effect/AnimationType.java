package sh.ball.audio.effect;

public enum AnimationType {
  STATIC("Static"),
  SEESAW("Seesaw"),
  LINEAR("Linear"),
  FORWARD("Forward"),
  REVERSE("Reverse");

  private final String name;

  AnimationType(String name) {
    this.name = name;
  }

  public static AnimationType fromString(String name) {
    for (AnimationType type : AnimationType.values()) {
      if (type.name.equals(name)) {
        return type;
      }
    }
    return null;
  }

  @Override
  public String toString() {
    return name;
  }
}
