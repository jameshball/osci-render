package sh.ball.audio.effect;

public enum AnimationType {
  STATIC("Static", "Static"),
  SINE("Sine", "Sine"),
  SQUARE("Square", "Square"),
  SEESAW("Seesaw", "Seesaw"),
  LINEAR("Linear", "Triangle"),
  FORWARD("Forward", "Sawtooth"),
  REVERSE("Reverse", "Reverse Sawtooth");

  private final String name;
  private final String displayName;

  AnimationType(String name, String displayName) {
    this.name = name;
    this.displayName = displayName;
  }

  public static AnimationType fromString(String name) {
    for (AnimationType type : AnimationType.values()) {
      if (type.name.equals(name)) {
        return type;
      }
    }
    return null;
  }

  public String getName() {
    return name;
  }

  @Override
  public String toString() {
    return displayName;
  }
}
