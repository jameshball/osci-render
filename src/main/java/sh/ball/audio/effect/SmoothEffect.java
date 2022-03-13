package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class SmoothEffect implements SettableEffect {

  private static final int MAX_WINDOW_SIZE = 2048;

  private final Vector2[] window;
  private int windowSize;
  private int head = 0;

  public SmoothEffect(int windowSize) {
    this.windowSize = windowSize <= 0 ? 1 : windowSize;
    this.window = new Vector2[MAX_WINDOW_SIZE];
  }

  @Override
  public synchronized void setValue(double value) {
    int windowSize = (int) (256 * value);
    this.windowSize = Math.max(1, Math.min(MAX_WINDOW_SIZE, windowSize));
  }

  // could be made much more efficient by just subbing prev vector and adding
  // new vector to the aggregate previous average
  @Override
  public synchronized Vector2 apply(int count, Vector2 vector) {
    window[head++] = vector;
    if (head >= MAX_WINDOW_SIZE) {
      head = 0;
    }
    double totalX = 0;
    double totalY = 0;
    int newHead = head - 1;
    for (int i = 0; i < windowSize; i++) {
      if (newHead < 0) {
        newHead = MAX_WINDOW_SIZE - 1;
      }

      if (window[newHead] != null) {
        totalX += window[newHead].getX();
        totalY += window[newHead].getY();
      }

      newHead--;
    }

    return new Vector2(totalX / windowSize, totalY / windowSize);
  }
}
