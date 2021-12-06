package sh.ball.audio.effect;

import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;

public class SmoothEffect implements Effect {

  private List<Vector2> window;
  private int windowSize;
  private int head = 0;

  public SmoothEffect(int windowSize) {
    this.windowSize = windowSize <= 0 ? 1 : windowSize;
    this.window = new ArrayList<>();
    for (int i = 0; i < windowSize; i++) {
      window.add(null);
    }
  }

  public synchronized void setWindowSize(int windowSize) {
    int oldWindowSize = this.windowSize;
    this.windowSize = windowSize <= 0 ? 1 : windowSize;
    List<Vector2> newWindow = new ArrayList<>();
    for (int i = 0; i < this.windowSize; i++) {
      newWindow.add(null);
    }
    for (int i = 0; i < Math.min(this.windowSize, oldWindowSize); i++) {
      newWindow.set(i, window.get(head++));
      if (head >= window.size()) {
        head = 0;
      }
    }
    head = 0;
    window = newWindow;
  }

  @Override
  public synchronized Vector2 apply(int count, Vector2 vector) {
    window.set(head, vector);
    head++;
    if (head >= windowSize) {
      head = 0;
    }
    double totalX = 0;
    double totalY = 0;
    int size = 0;

    for (Vector2 v : window) {
      if (v != null) {
        totalX += v.getX();
        totalY += v.getY();
        size++;
      }
    }

    return new Vector2(totalX / size, totalY / size);
  }
}
