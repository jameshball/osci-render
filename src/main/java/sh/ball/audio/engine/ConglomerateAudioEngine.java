package sh.ball.audio.engine;

import sh.ball.shapes.Vector2;

import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.Callable;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

// combines all types of AudioEngines into a single AudioEngine to allow for
// maximum compatibility and support for all audio drivers that have been
// implemented
public class ConglomerateAudioEngine implements AudioEngine {

  // used to determine which OS we are on
  private static final String OS = System.getProperty("os.name", "generic").toLowerCase(Locale.ENGLISH);
  private static final boolean MAC_OS = OS.contains("mac") || OS.contains("darwin");

  private final XtAudioEngine xtEngine = new XtAudioEngine();
  private final JavaAudioEngine javaEngine = new JavaAudioEngine();

  // TODO: Try and make non-static
  private static List<AudioDevice> xtDevices;
  private static List<AudioDevice> javaDevices;

  private boolean playing = false;
  private AudioDevice outputDevice;
  private AudioDevice inputDevice;
  private BufferedChannelGenerator bufferedChannelGenerator;

  @Override
  public boolean isPlaying() {
    return playing;
  }

  @Override
  public void play(Callable<Vector2> channelGenerator, AudioDevice device) throws Exception {
    playing = true;
    this.outputDevice = device;
    if (bufferedChannelGenerator != null) {
      bufferedChannelGenerator.stop();
    }
    bufferedChannelGenerator = new BufferedChannelGenerator(channelGenerator);
    new Thread(bufferedChannelGenerator).start();
    if (xtDevices.contains(device)) {
      xtEngine.play(bufferedChannelGenerator, device);
    } else {
      javaEngine.play(bufferedChannelGenerator, device);
    }
    playing = false;
    this.outputDevice = null;
  }

  @Override
  public void stop() {
    xtEngine.stop();
    javaEngine.stop();
    bufferedChannelGenerator.stop();
  }

  @Override
  public List<AudioDevice> devices() {
    List<AudioDevice> devices = new ArrayList<>();

    if (javaDevices == null) {
      javaDevices = javaEngine.devices();
    }

    if (xtDevices == null) {
      // XtAudio does not support MacOS so we should ignore all devices on XtAudio if on a mac
      if (MAC_OS) {
        xtDevices = new ArrayList<>();
      } else {
        xtDevices = xtEngine.devices();
      }
    }

    devices.addAll(javaDevices);
    devices.addAll(xtDevices);

    return devices;
  }

  @Override
  public AudioDevice getDefaultOutputDevice() {
    return javaEngine.getDefaultOutputDevice();
  }

  @Override
  public AudioDevice getDefaultInputDevice() {
    return javaEngine.getDefaultInputDevice();
  }

  @Override
  public AudioDevice currentOutputDevice() {
    return outputDevice;
  }

  @Override
  public AudioDevice currentInputDevice() {
    return inputDevice;
  }

  @Override
  public void setBrightness(double brightness) {
    javaEngine.setBrightness(brightness);
    xtEngine.setBrightness(brightness);
  }

  @Override
  public void addListener(AudioInputListener listener) {
    javaEngine.addListener(listener);
    xtEngine.addListener(listener);
  }

  @Override
  public void listen(AudioDevice device) {
    this.inputDevice = device;
    if (xtDevices.contains(device)) {
      xtEngine.listen(device);
    } else {
      javaEngine.listen(device);
    }
    this.inputDevice = null;
  }

  @Override
  public boolean inputAvailable() {
    return javaEngine.inputAvailable() || xtEngine.inputAvailable();
  }

  // This ensures that the channelGenerator is ALWAYS being called regardless
  // of whether the underlying AudioEngine is requesting channels as soon as they
  // are needed. This significantly improves performance of JavaAudioEngine and
  // keeps expensive computation out of the audio thread, in exchange for
  // potentially blocking on a buffer.
  private static class BufferedChannelGenerator implements Callable<Vector2>, Runnable {

    private static final int BLOCK_SIZE = 1024;
    private static final int BUFFER_SIZE = 2;

    private final ArrayBlockingQueue<Vector2[]> buffer;
    private final Callable<Vector2> channelGenerator;

    private Vector2[] currentBlock = null;
    private int currentVector = 0;
    private boolean stopped = true;

    private BufferedChannelGenerator(Callable<Vector2> channelGenerator) {
      this.buffer = new ArrayBlockingQueue<>(BUFFER_SIZE);
      this.channelGenerator = channelGenerator;
    }

    public void stop() {
      stopped = true;
    }

    @Override
    public void run() {
      stopped = false;
      while (!stopped) {
        Vector2[] block = new Vector2[BLOCK_SIZE];
        for (int i = 0; i < block.length; i++) {
          try {
            block[i] = channelGenerator.call();
          } catch (Exception e) {
            logger.log(Level.SEVERE, e.getMessage(), e);
          }
        }
        try {
          buffer.put(block);
        } catch (InterruptedException e) {
          logger.log(Level.SEVERE, e.getMessage(), e);
        }
      }
    }

    @Override
    public Vector2 call() throws Exception {
      if (currentBlock == null || currentVector >= BLOCK_SIZE) {
        currentBlock = buffer.take();
        currentVector = 0;
      }
      return currentBlock[currentVector++];
    }
  }
}
