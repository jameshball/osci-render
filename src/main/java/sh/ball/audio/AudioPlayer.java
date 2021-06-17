package sh.ball.audio;

import sh.ball.audio.effect.Effect;
import sh.ball.audio.engine.AudioDevice;

import javax.sound.sampled.AudioInputStream;
import java.util.List;

public interface AudioPlayer<S> extends Runnable {

  void reset() throws Exception;

  void stop();

  boolean isPlaying();

  void setQuality(double quality);

  void addFrame(S frame);

  void addEffect(Object identifier, Effect effect);

  void removeEffect(Object identifier);

  void read(byte[] buffer) throws InterruptedException;

  void startRecord();

  void setDevice(AudioDevice device);

  AudioDevice getDefaultDevice();

  AudioDevice getDevice();

  List<AudioDevice> devices();

  AudioInputStream stopRecord();
}
