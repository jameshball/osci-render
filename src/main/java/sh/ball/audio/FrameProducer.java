package sh.ball.audio;

public class FrameProducer<S> implements Runnable {

  private final AudioPlayer<S> audioPlayer;
  private final FrameSource<S> frames;

  public FrameProducer(AudioPlayer<S> audioPlayer, FrameSource<S> frames) {
    this.audioPlayer = audioPlayer;
    this.frames = frames;
  }

  @Override
  public void run() {
    while (frames.isActive()) {
      audioPlayer.addFrame(frames.next());
    }
  }

  public void setFrameSettings(Object settings) {
    frames.setFrameSettings(settings);
  }

  public Object getFrameSettings() {
    return frames.getFrameSettings();
  }
}
