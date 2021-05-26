package sh.ball.audio;

public class FrameProducer<S, T> implements Runnable {

  private final Renderer<S, T> renderer;
  private final FrameSet<S> frames;

  private boolean running;

  public FrameProducer(Renderer<S, T> renderer, FrameSet<S> frames) {
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

  public void setFrameSettings(Object settings) {
    frames.setFrameSettings(settings);
  }
}
