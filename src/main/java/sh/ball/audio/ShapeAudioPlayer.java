package sh.ball.audio;

import sh.ball.audio.effect.*;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.logging.Level;

import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.AudioEngine;
import sh.ball.audio.engine.AudioSample;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiNote;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import javax.sound.midi.ShortMessage;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;

import static sh.ball.audio.effect.EffectType.TRACE_MAX;
import static sh.ball.audio.effect.EffectType.TRACE_MIN;
import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.gui.Gui.logger;

public class ShapeAudioPlayer implements AudioPlayer<List<Shape>> {

  private static final double MIN_TRACE = 0.001;
  // Arbitrary max count for effects
  private static final int MAX_COUNT = 10000;
  private static final int BUFFER_SIZE = 10;

  private static final boolean BIG_ENDIAN = false;
  private static final double MIN_LENGTH_INCREMENT = 0.000001;

  // MIDI
  private final short[][] keyTargetVolumes = new short[MidiNote.NUM_CHANNELS][128];
  private final short[][] keyActualVolumes = new short[MidiNote.NUM_CHANNELS][128];
  private final AtomicInteger numKeysDown = new AtomicInteger(1);
  private final SineEffect[][] sineEffects = new SineEffect[MidiNote.NUM_CHANNELS][128];
  private boolean midiStarted = false;
  private int mainChannel = 0;
  private MidiNote baseNote = new MidiNote(60, mainChannel);
  private final double[] pitchBends = new double[MidiNote.NUM_CHANNELS];
  private int lastDecay = 0;
  private double decaySeconds = 0.2;
  private int decayFrames;
  private int lastAttack = 0;
  private double attackSeconds = 0.1;
  private int attackFrames;

  private final Callable<AudioEngine> audioEngineBuilder;
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private FrameSource<Vector2> sampleSource;
  private final List<EffectTypePair> effects = new CopyOnWriteArrayList<>();
  private final Queue<Listener> listeners = new ConcurrentLinkedQueue<>();

  private AudioEngine audioEngine;
  private ByteArrayOutputStream outputStream;
  private boolean recording = false;
  private int framesRecorded = 0;
  private List<Shape> frame;
  private double frameLength;
  private double frameDrawn = 0;
  private int currentShape = 0;
  private double lengthIncrement = MIN_LENGTH_INCREMENT;
  private double shapeDrawn = 0;
  private int count = 0;
  private double volume = 1;
  private double octaveFrequency;
  private double backingMidiVolume = 0.25;
  private double baseFrequency;
  private double traceMin = 0;
  private double traceMax = 1;
  private boolean traceMinEnabled = false;
  private boolean traceMaxEnabled = false;
  private int octave = 0;
  private int sampleRate;
  private boolean flipX = false;
  private boolean flipY = false;
  private double brightness = 1.0;
  private AudioSample audioSample = AudioSample.INT16;
  private double threshold = 1.0;

  private AudioDevice device;

  public ShapeAudioPlayer(Callable<AudioEngine> audioEngineBuilder, MidiCommunicator communicator) throws Exception {
    this.audioEngineBuilder = audioEngineBuilder;
    this.audioEngine = audioEngineBuilder.call();
    Arrays.fill(pitchBends, 1.0);
    resetMidi();

    communicator.addListener(this);
  }

  public void setAudioSample(AudioSample sample) {
    this.audioSample = sample;
  }
  public boolean midiPlaying() {
    return numKeysDown.get() > 0;
  }

  public void resetMidi() {
    for (int i = 0; i < MidiNote.NUM_CHANNELS; i++) {
      Arrays.fill(keyTargetVolumes[i], (short) 0);
      Arrays.fill(keyActualVolumes[i], (short) 0);
    }
    // Middle C is down by default
    keyTargetVolumes[0][60] = (short) MidiNote.MAX_VELOCITY;
    keyActualVolumes[0][60] = (short) MidiNote.MAX_VELOCITY;
    midiStarted = false;
    notesChanged();
  }

  public void stopMidiNotes() {
    for (int i = 0; i < MidiNote.NUM_CHANNELS; i++) {
      Arrays.fill(keyTargetVolumes[i], (short) 0);
    }
  }

  public void setFrequency(double frequency) {
    setBaseFrequency(frequency);
  }

  public void setVolume(double volume) {
    this.volume = volume;
  }

  public void setThreshold(double threshold) {
    this.threshold = threshold;
  }

  private void incrementShapeDrawing() {
    double length = getCurrentShape().getLength();

    frameDrawn += lengthIncrement;
    shapeDrawn += lengthIncrement;

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
      shapeDrawn -= length;
      currentShape++;
      // otherwise, index out of bounds
      if (currentShape >= frame.size()) {
        currentShape = 0;
        break;
      }
      length = getCurrentShape().getLength();
    }
  }

  private Vector2 generateChannels() {
    Vector2 channels;

    if (sampleSource != null) {
      channels = sampleSource.next();
    } else {
      Shape shape = getCurrentShape();

      double length = shape.getLength();
      double drawingProgress = length == 0 ? 1 : shapeDrawn / length;
      channels = shape.nextVector(drawingProgress);
    }

    channels = applyEffects(count, channels);
    channels = cutoff(channels);

    if (flipX) {
      channels = channels.setX(-channels.getX());
    }
    if (flipY) {
      channels = channels.setY(-channels.getY());
    }
    writeChannels((float) channels.getX(), (float) channels.getY());

    if (++count > MAX_COUNT) {
      count = 0;
    }

    incrementShapeDrawing();

    double actualTraceMax = traceMaxEnabled ? traceMax : 1.0;
    double actualTraceMin = traceMinEnabled ? traceMin : 0.0;

    double proportionalLength = actualTraceMax * frameLength;

    if (currentShape >= frame.size() || frameDrawn > proportionalLength) {
      currentShape = 0;
      List<Shape> oldFrame = frame;
      frame = frameQueue.poll();
      if (frame == null) {
        frame = oldFrame;
      } else {
        frameLength = Shape.totalLength(frame);
      }
      shapeDrawn = 0;
      frameDrawn = 0;

      while (frameDrawn < actualTraceMin * frameLength) {
        incrementShapeDrawing();
      }
    }

    updateLengthIncrement();

    return channels;
  }

  private void writeChannels(float leftChannel, float rightChannel) {
    double[] channels = new double[device.channels()];
    Arrays.fill(channels, brightness);

    if (channels.length > 0) {
      channels[0] = leftChannel;
    }
    if (channels.length > 1) {
      channels[1] = rightChannel;
    }

    if (recording) {
      AudioSample.writeAudioSample(audioSample, channels, outputStream::write);
    }

    int left = (int) (leftChannel * Short.MAX_VALUE);
    int right = (int) (rightChannel * Short.MAX_VALUE);

    byte b0 = (byte) left;
    byte b1 = (byte) (left >> 8);
    byte b2 = (byte) right;
    byte b3 = (byte) (right >> 8);

    for (Listener listener : listeners) {
      if (listener.isDoubleBuffer) {
        listener.write(leftChannel);
        listener.write(rightChannel);
      } else {
        listener.write(b0);
        listener.write(b1);
        listener.write(b2);
        listener.write(b3);
      }
      listener.notifyIfFull();
    }

    framesRecorded++;
  }

  private Vector2 cutoff(Vector2 vector) {
    double x = vector.x;
    double y = vector.y;
    if (x < -threshold) {
      x = -threshold;
    } else if (x > threshold) {
      x = threshold;
    }
    if (y < -threshold) {
      y = -threshold;
    } else if (y > threshold) {
      y = threshold;
    }
    return new Vector2(x, y);
  }

  private Vector2 applyEffects(int frame, Vector2 vector) {
    vector = vector.scale((double) keyActualVolumes[mainChannel][baseNote.key()] / MidiNote.MAX_VELOCITY);
    if (midiStarted) {
      if (lastDecay > decayFrames) {
        for (int i = 0; i < keyActualVolumes.length; i++) {
          for (int j = 0; j < keyActualVolumes[i].length; j++) {
            if (keyActualVolumes[i][j] > keyTargetVolumes[i][j]) {
              keyActualVolumes[i][j]--;
            }
          }
        }
        lastDecay = 0;
      }
      lastDecay++;
      if (lastAttack > attackFrames) {
        for (int i = 0; i < keyActualVolumes.length; i++) {
          for (int j = 0; j < keyActualVolumes[i].length; j++) {
            if (keyActualVolumes[i][j] < keyTargetVolumes[i][j]) {
              keyActualVolumes[i][j]++;
            }
          }
        }
        lastAttack = 0;
      }
      lastAttack++;

      for (int channel = 0; channel < keyActualVolumes.length; channel++) {
        for (int key = 0; key < keyActualVolumes[0].length; key++) {
          if (keyActualVolumes[channel][key] > 0 && !(baseNote.key() == key && baseNote.channel() == channel)) {
            Vector2 sine = sineEffects[channel][key].apply(frame, new Vector2());
            double volume = backingMidiVolume * keyActualVolumes[channel][key] / MidiNote.MAX_VELOCITY;
            vector = new Vector2(vector.x + volume * sine.x, vector.y + volume * sine.y);
          }
        }
      }
    }

    for (EffectTypePair pair : effects) {
      Effect effect = pair.effect();
      vector = effect.apply(frame, vector);
    }

    return vector.scale(volume);
  }

  private void setBaseFrequency(double baseFrequency) {
    this.baseFrequency = baseFrequency;
    this.octaveFrequency = baseFrequency * Math.pow(2, octave - 1);
    updateLengthIncrement();
    updateSineEffects();
  }

  private void setPitchBendFactor(int channel, double pitchBend) {
    pitchBends[channel] = pitchBend;
    updateLengthIncrement();
    updateSineEffects();
  }

  @Override
  public void setDecay(double decaySeconds) {
    this.decaySeconds = decaySeconds;
    updateDecay();
  }

  @Override
  public void setAttack(double attackSeconds) {
    this.attackSeconds = attackSeconds;
    updateAttack();
  }

  private Shape getCurrentShape() {
    if (frame.size() == 0) {
      return new Vector2();
    }

    return frame.get(currentShape);
  }

  private void updateLengthIncrement() {
    if (frame != null) {
      double actualTraceMax = traceMaxEnabled ? traceMax : 1.0;
      double actualTraceMin = traceMinEnabled ? traceMin : 0.0;
      double proportionalLength = (actualTraceMax - actualTraceMin) * frameLength;
      int sampleRate = device.sampleRate();
      double actualFrequency = octaveFrequency * pitchBends[mainChannel];
      lengthIncrement = Math.max(proportionalLength / (sampleRate / actualFrequency), MIN_LENGTH_INCREMENT);
    }
  }

  @Override
  public void run() {
    try {
      if (sampleSource == null) {
        frame = frameQueue.take();
        frameLength = Shape.totalLength(frame);
        updateLengthIncrement();
      }
    } catch (InterruptedException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
      RuntimeException exception = new RuntimeException("Initial frame not found. Cannot continue.");
      logger.log(Level.SEVERE, exception.getMessage(), exception);
      throw exception;
    }

    if (device == null) {
      IllegalArgumentException e = new IllegalArgumentException("No AudioDevice provided");
      logger.log(Level.SEVERE, e.getMessage(), e);
      throw e;
    }

    try {
      audioEngine.play(this::generateChannels, device);
    } catch (Exception e) {
      e.printStackTrace();
    }
  }

  @Override
  public void reset() throws Exception {
    audioEngine.stop();
    while (isPlaying()) {
      Thread.onSpinWait();
    }
    audioEngine = audioEngineBuilder.call();
  }

  @Override
  public void stop() {
    audioEngine.stop();
  }

  @Override
  public boolean isPlaying() {
    return audioEngine.isPlaying();
  }

  @Override
  public void setOctave(int octave) {
    this.octave = octave;
    octaveFrequency = baseFrequency * Math.pow(2, octave - 1);
  }

  @Override
  public void setBackingMidiVolume(double scale) {
    this.backingMidiVolume = scale;
    updateSineEffects();
  }

  public void setMainMidiChannel(int channel) {
    this.mainChannel = channel;
    notesChanged();
  }

  @Override
  public void addFrame(List<Shape> frame) {
    try {
      frameQueue.put(frame);
    } catch (InterruptedException e) {
      logger.log(Level.SEVERE, "Frame missed", e);
    }
  }

  @Override
  public void addEffect(EffectType type, Effect effect) {
    if (type.equals(TRACE_MAX)) {
      traceMaxEnabled = true;
    }
    if (type.equals(TRACE_MIN)) {
      traceMinEnabled = true;
    }
    effects.add(new EffectTypePair(type, effect));
    Collections.sort(effects);
  }

  @Override
  public void removeEffect(EffectType type) {
    if (type.equals(TRACE_MAX)) {
      traceMaxEnabled = false;
    }
    if (type.equals(TRACE_MIN)) {
      traceMinEnabled = false;
    }
    effects.removeIf(e -> e.type == type);
  }

  // selects or deselects the given audio effect
  public void updateEffect(EffectType type, boolean checked, SettableEffect effect) {
    if (type != null) {
      if (checked) {
        audioPlayer.addEffect(type, effect);
      } else {
        audioPlayer.removeEffect(type);
      }
    }
  }

  @Override
  public void read(byte[] buffer) throws InterruptedException {
    Listener listener = new Listener(buffer);
    listeners.add(listener);
    listener.waitUntilFull();
    listeners.remove(listener);
  }

  @Override
  public void read(double[] buffer) throws InterruptedException {
    Listener listener = new Listener(buffer);
    listeners.add(listener);
    listener.waitUntilFull();
    listeners.remove(listener);
  }

  @Override
  public void startRecord() {
    outputStream = new ByteArrayOutputStream();
    framesRecorded = 0;
    recording = true;
  }

  @Override
  public void setDevice(AudioDevice device) {
    this.device = device;
    for (int i = 0; i < MidiNote.NUM_CHANNELS; i++) {
      Arrays.fill(sineEffects[i], null);
    }
    this.sampleRate = device.sampleRate();
    for (int channel = 0; channel < keyActualVolumes.length; channel++) {
      for (int key = 0; key < keyActualVolumes[channel].length; key++) {
        MidiNote note = new MidiNote(key, channel);
        sineEffects[channel][key] = new SineEffect(sampleRate, note.frequency());
      }
    }
    for (EffectTypePair pair : effects) {
      Effect effect = pair.effect();
      if (effect instanceof PhaseEffect phase) {
        phase.setSampleRate(sampleRate);
      }
    }
    updateDecay();
    updateAttack();
  }

  private void updateDecay() {
    this.decayFrames = (int) (decaySeconds * sampleRate / MidiNote.MAX_VELOCITY);
  }

  private void updateAttack() {
    this.attackFrames = (int) (attackSeconds * sampleRate / MidiNote.MAX_VELOCITY);
  }

  @Override
  public AudioDevice getDefaultDevice() {
    return audioEngine.getDefaultDevice();
  }

  @Override
  public AudioDevice getDevice() {
    return device;
  }

  @Override
  public List<AudioDevice> devices() {
    return audioEngine.devices();
  }

  @Override
  public AudioInputStream stopRecord() {
    recording = false;
    if (outputStream == null) {
      throw new UnsupportedOperationException("Cannot stop recording before first starting to record");
    }
    byte[] input = outputStream.toByteArray();
    outputStream = null;

    AudioFormat audioFormat = new AudioFormat(sampleRate, audioSample.size, device.channels(), audioSample.signed, BIG_ENDIAN);

    return new AudioInputStream(new ByteArrayInputStream(input), audioFormat, framesRecorded);
  }

  @Override
  public void setBrightness(double brightness) {
    this.brightness = brightness;
    audioEngine.setBrightness(brightness);
  }

  private void updateSineEffects() {
    for (int channel = 0; channel < keyActualVolumes.length; channel++) {
      for (int key = 0; key < keyActualVolumes[channel].length; key++) {
        MidiNote note = new MidiNote(key, channel);
        if (keyActualVolumes[channel][key] > 0) {
          SineEffect effect = sineEffects[channel][key];
          if (effect != null) {
            effect.setFrequency(note.frequency() * pitchBends[channel]);
          }
        }
      }
    }
  }

  private void notesChanged() {
    for (int key = keyTargetVolumes[mainChannel].length - 1; key >= 0; key--) {
      if (keyTargetVolumes[mainChannel][key] > 0) {
        MidiNote note = new MidiNote(key, mainChannel);
        setBaseFrequency(note.frequency());
        baseNote = note;
        return;
      }
    }
  }

  public void setTraceMin(double traceMin) {
    this.traceMin = Math.max(MIN_TRACE, Math.min(traceMin, traceMax - MIN_TRACE));
  }

  public void setTraceMax(double traceMax) {
    this.traceMax = Math.max(traceMin + MIN_TRACE, Math.min(traceMax, 1));
  }

  @Override
  public synchronized void sendMidiMessage(ShortMessage message) {
    if (!isPlaying()) {
      return;
    }
    int command = message.getCommand();

    if (!midiStarted && (command == ShortMessage.NOTE_ON || command == ShortMessage.NOTE_OFF)) {
      stopMidiNotes();
      midiStarted = true;
    }

    if (command == ShortMessage.NOTE_ON || command == ShortMessage.NOTE_OFF) {
      MidiNote note = new MidiNote(message.getData1(), message.getChannel());
      int velocity = message.getData2();

      if (command == ShortMessage.NOTE_OFF || velocity == 0) {
        keyTargetVolumes[note.channel()][note.key()] = 0;
        numKeysDown.getAndDecrement();
      } else {
        keyTargetVolumes[note.channel()][note.key()] = (short) velocity;
        numKeysDown.getAndIncrement();
      }
      notesChanged();
    } else if (command == ShortMessage.PITCH_BEND) {
      // using these instructions https://sites.uci.edu/camp2014/2014/04/30/managing-midi-pitchbend-messages/

      int pitchBend = (message.getData2() << MidiNote.PITCH_BEND_DATA_LENGTH) | message.getData1();
      // get pitch bend in range -1 to 1
      double pitchBendFactor = (double) pitchBend / MidiNote.PITCH_BEND_MAX;
      pitchBendFactor = 2 * pitchBendFactor - 1;
      pitchBendFactor *= MidiNote.PITCH_BEND_SEMITONES;
      // 12 tone equal temperament
      pitchBendFactor /= 12;
      pitchBendFactor = Math.pow(2, pitchBendFactor);

      setPitchBendFactor(message.getChannel(), pitchBendFactor);
    }
  }

  public void flipXChannel(boolean flip) {
    this.flipX = flip;
  }

  public void flipYChannel(boolean flip) {
    this.flipY = flip;
  }

  public void setSampleSource(FrameSource<Vector2> sampleSource) {
    this.sampleSource = sampleSource;
  }

  public void removeSampleSource() {
    this.sampleSource = null;
  }

  private static class Listener {
    private final byte[] buffer;
    private final double[] doubleBuffer;
    private final boolean isDoubleBuffer;
    private final Semaphore sema;

    private int offset;

    private Listener(byte[] buffer) {
      this.buffer = buffer;
      this.doubleBuffer = null;
      this.isDoubleBuffer = false;
      this.sema = new Semaphore(0);
    }

    private Listener(double[] buffer) {
      this.buffer = null;
      this.doubleBuffer = buffer;
      this.isDoubleBuffer = true;
      this.sema = new Semaphore(0);
    }

    private void waitUntilFull() throws InterruptedException {
      sema.acquire();
    }

    private void notifyIfFull() {
      if (doubleBuffer != null && offset >= doubleBuffer.length || buffer != null && offset >= buffer.length) {
        sema.release();
      }
    }

    private void write(byte b) {
      if (buffer != null && offset < buffer.length) {
        buffer[offset++] = b;
      }
    }

    private void write(double d) {
      if (doubleBuffer != null && offset < doubleBuffer.length) {
        doubleBuffer[offset++] = d;
      }
    }
  }

  private record EffectTypePair(EffectType type, Effect effect) implements Comparable<EffectTypePair> {

    @Override
    public int compareTo(EffectTypePair pair) {
      return Integer.compare(type.precedence, pair.type.precedence);
    }
  }
}
