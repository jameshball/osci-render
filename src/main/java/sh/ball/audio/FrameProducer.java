package sh.ball.audio;

public class FrameProducer<T> implements Runnable {

  private final Renderer<T> renderer;
  private final FrameSet<T> frames;

  private boolean running;

  public FrameProducer(Renderer<T> renderer, FrameSet<T> frames) {
    this.renderer = renderer;
    this.frames = frames;
  }

  @Override
  public void run() {
    running = true;
    while (running) {
      renderer.addFrame(frames.next());
    }
  }

  public void stop() {
    running = false;
  }

  public Object setFrameSettings(Object settings, boolean flushFrames) {
    if (flushFrames) {
      renderer.flushFrames();
    }
    return frames.setFrameSettings(settings);
  }
}
