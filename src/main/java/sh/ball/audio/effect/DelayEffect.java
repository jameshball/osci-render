package sh.ball.audio.effect;

/*
 *      _______                       _____   _____ _____
 *     |__   __|                     |  __ \ / ____|  __ \
 *        | | __ _ _ __ ___  ___  ___| |  | | (___ | |__) |
 *        | |/ _` | '__/ __|/ _ \/ __| |  | |\___ \|  ___/
 *        | | (_| | |  \__ \ (_) \__ \ |__| |____) | |
 *        |_|\__,_|_|  |___/\___/|___/_____/|_____/|_|
 *
 * -------------------------------------------------------------
 *
 * TarsosDSP is developed by Joren Six at IPEM, University Ghent
 *
 * -------------------------------------------------------------
 *
 *  Info: http://0110.be/tag/TarsosDSP
 *  Github: https://github.com/JorenSix/TarsosDSP
 *  Releases: http://0110.be/releases/TarsosDSP/
 *
 *  TarsosDSP includes modified source code by various authors,
 *  for credits and info, see README.
 *
 *
 *  THIS IS A MODIFIED VERSION BY JAMES H BALL FOR OSCI-RENDER
 *
 *
 */


import java.util.Arrays;
import sh.ball.shapes.Vector2;

/**
 * <p>
 * Adds an echo effect to the signal.
 * </p>
 *
 * @author Joren Six
 */
public class DelayEffect implements SettableEffect {

  private final static int MAX_DELAY = 192000 * 10;

  private final Vector2[] echoBuffer;
  private double sampleRate;
  private int head;
  private double decay;
  private double echoLength;
  private int position;

  private int echoBufferLength;
  private int samplesSinceLastEcho = 0;

  /**
   * @param echoLength in seconds
   * @param sampleRate the sample rate in Hz.
   * @param decay The decay of the echo, a value between 0 and 1. 1 meaning no decay, 0 means immediate decay (not echo effect).
   */
  public DelayEffect(double echoLength, double decay, double sampleRate) {
    this.sampleRate = sampleRate;
    this.echoBuffer = new Vector2[MAX_DELAY];
    Arrays.fill(echoBuffer, new Vector2());
    this.decay = decay;
    setEchoLength(echoLength);
  }

  /**
   * @param newEchoLength A new echo buffer length in seconds.
   */
  public void setEchoLength(double newEchoLength) {
    this.echoLength = newEchoLength;
    this.echoBufferLength = (int) (echoLength * sampleRate);
  }

  public void setSampleRate(double sampleRate){
    this.sampleRate = sampleRate;
    this.echoBufferLength = (int) (echoLength * sampleRate);
  }

  @Override
  public Vector2 apply(int count, Vector2 vector) {
    if (head >= echoBuffer.length){
      head = 0;
    }
    if (position >= echoBuffer.length){
      position = 0;
    }
    if (samplesSinceLastEcho >= echoBufferLength) {
      samplesSinceLastEcho = 0;
      position = head - echoBufferLength;
      if (position < 0) {
        position += echoBuffer.length;
      }
    }

    Vector2 echo = echoBuffer[position];
    vector = new Vector2(
        vector.x + echo.x * decay,
        vector.y + echo.y * decay
    );

    echoBuffer[head] = vector;

    head++;
    position++;
    samplesSinceLastEcho++;

    return vector;
  }

  @Override
  public void setValue(double value) {
    decay = value;
  }
}