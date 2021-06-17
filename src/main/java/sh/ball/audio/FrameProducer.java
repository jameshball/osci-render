package sh.ball.audio;

public class FrameProducer<S> implements Runnable {

  private final AudioPlayer<S> audioPlayer;
  private final FrameSet<S> frames;

  private boolean running;

  public FrameProducer(AudioPlayer<S> audioPlayer, FrameSet<S> frames) {
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
