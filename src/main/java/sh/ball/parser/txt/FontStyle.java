package sh.ball.parser.txt;

import java.awt.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public enum FontStyle {
  PLAIN(Font.PLAIN),
  BOLD(Font.BOLD),
  ITALIC(Font.ITALIC);

  public final int style;

  FontStyle(int style) {
    this.style = style;
  }

  public static FontStyle getStyle(int style) {
    switch (style) {
      case Font.PLAIN -> {
        return PLAIN;
      }
      case Font.BOLD -> {
        return BOLD;
      }
      case Font.ITALIC -> {
        return ITALIC;
      }
      default -> {
        logger.log(Level.WARNING, "Unknown font style: " + style);
        return PLAIN;
      }
    }
  }

  @Override
  public String toString() {
    return name().substring(0, 1).toUpperCase() + name().substring(1).toLowerCase();
  }
}
