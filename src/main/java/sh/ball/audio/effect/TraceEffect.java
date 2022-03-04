package sh.ball.audio.effect;

import sh.ball.audio.ShapeAudioPlayer;
import sh.ball.shapes.Vector2;

public class TraceEffect implements SettableEffect {

  private final ShapeAudioPlayer audioPlayer;

  public TraceEffect(ShapeAudioPlayer audioPlayer) {
    this.audioPlayer = audioPlayer;
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    return vector;
  }

  @Override
  public void setValue(double trace) {
    audioPlayer.setTrace(trace);
  }
}
