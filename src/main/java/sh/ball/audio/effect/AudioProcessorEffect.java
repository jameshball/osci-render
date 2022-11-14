package sh.ball.audio.effect;

import be.tarsos.dsp.AudioEvent;
import be.tarsos.dsp.AudioProcessor;
import be.tarsos.dsp.io.TarsosDSPAudioFormat;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.shapes.Vector2;

public class AudioProcessorEffect implements Effect {

  private static final boolean BIG_ENDIAN = false;
  private final AudioProcessor processor;
  private AudioEvent event;

  private final float[] buffer = new float[2];

  public AudioProcessorEffect(AudioProcessor processor) {
    this.processor = processor;
  }

  public void setDevice(AudioDevice device) {
    event = new AudioEvent(new TarsosDSPAudioFormat(device.sampleRate(), device.sample().size, device.channels(), device.sample().signed, BIG_ENDIAN));
    event.setFloatBuffer(buffer);
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (event == null) {
      return vector;
    }
    buffer[0] = (float) vector.x;
    buffer[1] = (float) vector.y;

    processor.process(event);

    return new Vector2(buffer[0], buffer[1]);
  }
}
