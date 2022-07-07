package sh.ball.parser.txt;

import java.awt.*;

public enum FontStyle {
  PLAIN(Font.PLAIN),
  BOLD(Font.BOLD),
  ITALIC(Font.ITALIC);

  public final int style;

  FontStyle(int style) {
    this.style = style;
  }

  @Override
  public String toString() {
    return name().substring(0, 1).toUpperCase() + name().substring(1).toLowerCase();
  }
}
