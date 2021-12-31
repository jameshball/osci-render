package sh.ball.audio;

import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import sh.ball.audio.effect.Effect;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;

import sh.ball.audio.effect.PhaseEffect;
import sh.ball.audio.effect.SineEffect;
import sh.ball.audio.effect.SmoothEffect;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.AudioEngine;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiNote;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import javax.sound.midi.ShortMessage;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;

import static sh.ball.gui.Gui.audioPlayer;

public class ShapeAudioPlayer implements AudioPlayer<List<Shape>> {

  private static final double MIN_TRACE = 0.001;
  // Arbitrary max count for effects
  private static final int MAX_COUNT = 10000;
  private static final int BUFFER_SIZE = 10;
  // Is this always true? Might need to check from AudioEngine
  private static final int BITS_PER_SAMPLE = 16;
  private static final boolean SIGNED = true;
  private static final boolean BIG_ENDIAN = false;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;
  private static final double MIN_LENGTH_INCREMENT = 0.0000000001;

  // MIDI
  private final short[][] keyTargetVolumes = new short[MidiNote.NUM_CHANNELS][128];
  private final short[][] keyActualVolumes = new short[MidiNote.NUM_CHANNELS][128];
  private final Set<MidiNote> keysDown = ConcurrentHashMap.newKeySet();
  private boolean midiStarted = false;
  private int mainChannel = 0;
  private MidiNote baseNote = new MidiNote(60, mainChannel);
  private double[] pitchBends = new double[MidiNote.NUM_CHANNELS];
  private int lastDecay = 0;
  private double decaySeconds = 0.2;
  private int decayFrames;
  private int lastAttack = 0;
  private double attackSeconds = 0.1;
  private int attackFrames;

  private final Callable<AudioEngine> audioEngineBuilder;
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final Map<Object, Effect> effects = new ConcurrentHashMap<>();
  private final Map<MidiNote, SineEffect> sineEffects = new ConcurrentHashMap<>();
  private final List<Listener> listeners = new CopyOnWriteArrayList<>();

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
  private DoubleProperty volume = new SimpleDoubleProperty(1);
  private double octaveFrequency;
  private DoubleProperty frequency;
  private double baseFrequencyVolumeScale = 0.5;
  private double baseFrequency;
  private double trace = 1;
  private int octave = 0;
  private int sampleRate;

  private AudioDevice device;

  public ShapeAudioPlayer(Callable<AudioEngine> audioEngineBuilder, MidiCommunicator communicator) throws Exception {
    this.audioEngineBuilder = audioEngineBuilder;
    this.audioEngine = audioEngineBuilder.call();
    Arrays.fill(pitchBends, 1.0);
    setFrequency(new SimpleDoubleProperty(0));
    resetMidi();

    communicator.addListener(this);
  }

  private void resetMidi() {
    keysDown.clear();
    for (int i = 0; i < keyTargetVolumes.length; i++) {
      Arrays.fill(keyTargetVolumes[i], (short) 0);
      Arrays.fill(keyActualVolumes[i], (short) 0);
    }
    // Middle C is down by default
    keyTargetVolumes[0][60] = (short) MidiNote.MAX_VELOCITY;
    keyActualVolumes[0][60] = (short) MidiNote.MAX_VELOCITY;
    keysDown.add(new MidiNote(60));
    midiStarted = false;
  }

  public void stopMidiNotes() {
    keysDown.clear();
    for (short[] keyTargetVolume : keyTargetVolumes) {
      Arrays.fill(keyTargetVolume, (short) 0);
    }
  }

  public void setFrequency(DoubleProperty frequency) {
    this.frequency = frequency;
    frequency.addListener((o, old, f) -> setBaseFrequency(f.doubleValue()));
  }

  public void setVolume(DoubleProperty volume) {
    this.volume = Objects.requireNonNullElseGet(volume, () -> new SimpleDoubleProperty(1));
  }

  private Vector2 generateChannels() throws InterruptedException {
    Shape shape = getCurrentShape();

    double length = shape.getLength();
    double drawingProgress = length == 0 ? 1 : shapeDrawn / length;
    Vector2 nextVector = applyEffects(count, shape.nextVector(drawingProgress));

    Vector2 channels = cutoff(nextVector);
    writeChannels((float) channels.getX(), (float) channels.getY());

    shapeDrawn += lengthIncrement;
    frameDrawn += lengthIncrement;

    if (++count > MAX_COUNT) {
      count = 0;
    }

    // Need to skip all shapes that the lengthIncrement draws over.
    // This is especially an issue when there are lots of small lines being
    // drawn.
    while (shapeDrawn > length) {
      shapeDrawn -= length;
      currentShape++;
      // otherwise, index out of bounds
      if (currentShape >= frame.size()) {
        break;
      }
      length = getCurrentShape().getLength();
    }

    double proportionalLength = trace * frameLength;

    if (currentShape >= frame.size() || frameDrawn > proportionalLength) {
      currentShape = 0;
      frame = frameQueue.take();
      frameLength = Shape.totalLength(frame);
      shapeDrawn = 0;
      frameDrawn = 0;
    }

    updateLengthIncrement();

    return channels;
  }

  private void writeChannels(float leftChannel, float rightChannel) {
    int left = (int) (leftChannel * Short.MAX_VALUE);
    int right = (int) (rightChannel * Short.MAX_VALUE);

    byte b0 = (byte) left;
    byte b1 = (byte) (left >> 8);
    byte b2 = (byte) right;
    byte b3 = (byte) (right >> 8);

    if (recording) {
      outputStream.write(b0);
      outputStream.write(b1);
      outputStream.write(b2);
      outputStream.write(b3);
    }

    for (Listener listener : listeners) {
      listener.write(b0);
      listener.write(b1);
      listener.write(b2);
      listener.write(b3);
      listener.notifyIfFull();
    }

    framesRecorded++;
  }

  private Vector2 cutoff(Vector2 vector) {
    if (vector.getX() < -1) {
      vector = vector.setX(-1);
    } else if (vector.getX() > 1) {
      vector = vector.setX(1);
    }
    if (vector.getY() < -1) {
      vector = vector.setY(-1);
    } else if (vector.getY() > 1) {
      vector = vector.setY(1);
    }
    return vector;
  }

  private Vector2 applyEffects(int frame, Vector2 vector) {
    vector = vector.scale(2 * baseFrequencyVolumeScale * keyActualVolumes[mainChannel][baseNote.key()] / MidiNote.MAX_VELOCITY);
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
      for (MidiNote note : keysDown) {
        if (!note.equals(baseNote)) {
          vector = sineEffects.get(note).apply(frame, vector);
        }
      }
    }
    // Smooth effects MUST be applied last, consistently
    SmoothEffect smoothEffect = null;
    for (Effect effect : effects.values()) {
      if (effect instanceof SmoothEffect) {
        smoothEffect = (SmoothEffect) effect;
      } else {
        vector = effect.apply(frame, vector);
      }
    }

    if (smoothEffect != null) {
      vector = smoothEffect.apply(frame, vector);
    }

    return vector.scale(volume.get());
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
      double proportionalLength = trace * frameLength;
      int sampleRate = device.sampleRate();
      double actualFrequency = octaveFrequency * pitchBends[mainChannel];
      lengthIncrement = Math.max(proportionalLength / (sampleRate / actualFrequency), MIN_LENGTH_INCREMENT);
    }
  }

  @Override
  public void run() {
    try {
      frame = frameQueue.take();
      frameLength = Shape.totalLength(frame);
      updateLengthIncrement();
    } catch (InterruptedException e) {
      throw new RuntimeException("Initial frame not found. Cannot continue.");
    }

    if (device == null) {
      throw new IllegalArgumentException("No AudioDevice provided");
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
  public void setBaseFrequencyVolumeScale(double scale) {
    this.baseFrequencyVolumeScale = scale;
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
      e.printStackTrace();
      System.err.println("Frame missed.");
    }
  }

  @Override
  public void addEffect(Object identifier, Effect effect) {
    effects.put(identifier, effect);
  }

  @Override
  public void removeEffect(Object identifier) {
    effects.remove(identifier);
  }

  @Override
  public void read(byte[] buffer) throws InterruptedException {
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
    sineEffects.clear();
    this.sampleRate = device.sampleRate();
    for (int channel = 0; channel < keyActualVolumes.length; channel++) {
      for (int key = 0; key < keyActualVolumes[channel].length; key++) {
        MidiNote note = new MidiNote(key, channel);
        sineEffects.put(note, new SineEffect(sampleRate, note.frequency(), keyActualVolumes[channel][key] / MidiNote.MAX_VELOCITY));
      }
    }
    for (Effect effect : effects.values()) {
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

    AudioFormat audioFormat = new AudioFormat(sampleRate, BITS_PER_SAMPLE, NUM_OUTPUTS, SIGNED, BIG_ENDIAN);

    return new AudioInputStream(new ByteArrayInputStream(input), audioFormat, framesRecorded);
  }

  private void updateSineEffects() {
    double totalVolume = 0;
    for (short[] volumes : keyActualVolumes) {
      for (short volume : volumes) {
        totalVolume += volume / MidiNote.MAX_VELOCITY;
      }
    }
    if (totalVolume != 0) {
      double scaledVolume = (1 - baseFrequencyVolumeScale) / totalVolume;
      for (int channel = 0; channel < keyActualVolumes.length; channel++) {
        for (int key = 0; key < keyActualVolumes[channel].length; key++) {
          MidiNote note = new MidiNote(key, channel);
          if (keyActualVolumes[channel][key] > 0 && sineEffects.size() != 0) {
            SineEffect effect = sineEffects.get(note);
            effect.setVolume(scaledVolume * keyActualVolumes[channel][key] / MidiNote.MAX_VELOCITY);
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
        frequency.set(note.frequency());
        baseNote = note;
        updateSineEffects();
        return;
      }
    }
  }

  public void setTrace(double trace) {
    this.trace = Math.max(MIN_TRACE, Math.min(trace, 1));
  }

  @Override
  public synchronized void sendMidiMessage(ShortMessage message) {
    if (!isPlaying()) {
      return;
    }
    int command = message.getCommand();
    if (!midiStarted) {
      stopMidiNotes();
      midiStarted = true;
    }

    if (command == ShortMessage.NOTE_ON || command == ShortMessage.NOTE_OFF) {
      MidiNote note = new MidiNote(message.getData1(), message.getChannel());
      int velocity = message.getData2();

      if (command == ShortMessage.NOTE_OFF || velocity == 0) {
        keyTargetVolumes[note.channel()][note.key()] = 0;
        keysDown.remove(note);
      } else {
        keyTargetVolumes[note.channel()][note.key()] = (short) velocity;
        keysDown.add(note);
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

  private static class Listener {
    private final byte[] buffer;
    private final Semaphore sema;

    private int offset;

    private Listener(byte[] buffer) {
      this.buffer = buffer;
      this.sema = new Semaphore(0);
    }

    private void waitUntilFull() throws InterruptedException {
      sema.acquire();
    }

    private void notifyIfFull() {
      if (offset >= buffer.length) {
        sema.release();
      }
    }

    private void write(byte b) {
      if (offset < buffer.length) {
        buffer[offset++] = b;
      }
    }
  }
}
