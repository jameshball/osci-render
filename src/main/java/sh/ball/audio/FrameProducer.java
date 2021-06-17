package sh.ball.audio;

public class FrameProducer<S, T> implements Runnable {

  private final AudioPlayer<S, T> audioPlayer;
  private final FrameSet<S> frames;

  private boolean running;

  public FrameProducer(AudioPlayer<S, T> audioPlayer, FrameSet<S> frames) {
    this.audioPlayer = audioPlayer;
    this.frames = frames;
  }

  @Override
  public void run() {
    running = true;
    while (running) {
      audioPlayer.addFrame(frames.next());
    }
  }

  public void stop() {
    running = false;
  }

  public void setFrameSettings(Object settings) {
    frames.setFrameSettings(settings);
  }
}
