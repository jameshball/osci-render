package sh.ball.audio;

import javafx.animation.Interpolator;
import javafx.animation.KeyFrame;
import javafx.animation.KeyValue;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.util.Duration;
import sh.ball.audio.effect.Effect;

import java.io.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.stream.Collectors;

import sh.ball.audio.effect.SineEffect;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.AudioEngine;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiNote;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import javax.sound.midi.ShortMessage;
import javax.sound.sampled.AudioFormat;
import javax.sound.sampled.AudioInputStream;

public class ShapeAudioPlayer implements AudioPlayer<List<Shape>> {

  // Arbitrary max count for effects
  private static final int MAX_COUNT = 10000;
  private static final int BUFFER_SIZE = 5;
  // Is this always true? Might need to check from AudioEngine
  private static final int BITS_PER_SAMPLE = 16;
  private static final boolean SIGNED = true;
  private static final boolean BIG_ENDIAN = false;
  // Stereo audio
  private static final int NUM_OUTPUTS = 2;
  private static final double MIN_LENGTH_INCREMENT = 0.0000000001;
  private static final double MIDDLE_C = 261.63;

  // MIDI
  private static final int PITCH_BEND_DATA_LENGTH = 7;
  private static final int PITCH_BEND_MAX = 16383;
  private static final int PITCH_BEND_SEMITONES = 2;
  private final List<MidiNote> downKeys = new CopyOnWriteArrayList<>();
  private Timeline volumeTimeline;

  private final Callable<AudioEngine> audioEngineBuilder;
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final Map<Object, Effect> effects = new ConcurrentHashMap<>();
  private final List<SineEffect> sineEffects = new CopyOnWriteArrayList<>();
  private final List<Listener> listeners = new CopyOnWriteArrayList<>();

  private AudioEngine audioEngine;
  private ByteArrayOutputStream outputStream;
  private boolean recording = false;
  private int framesRecorded = 0;
  private List<Shape> frame;
  private int currentShape = 0;
  private double lengthIncrement = MIN_LENGTH_INCREMENT;
  private double lengthDrawn = 0;
  private int count = 0;
  private DoubleProperty volume;
  private List<Double> frequencies = List.of(MIDDLE_C);
  private double octaveFrequency;
  private DoubleProperty frequency;
  private double mainFrequencyScale = 0.5;
  private double maxFrequency;
  private double pitchBend = 1.0;
  private double trace = 0.5;
  private int octave = 0;

  private AudioDevice device;

  public ShapeAudioPlayer(Callable<AudioEngine> audioEngineBuilder, MidiCommunicator communicator) throws Exception {
    this.audioEngineBuilder = audioEngineBuilder;
    this.audioEngine = audioEngineBuilder.call();
    setFrequency(new SimpleDoubleProperty(0));
    this.maxFrequency = frequency.get();
    setFrequency(new SimpleDoubleProperty(0));

    communicator.addListener(this);
  }

  public void setFrequency(DoubleProperty frequency) {
    this.frequency = frequency;
    frequency.addListener((o, old, f) -> setBaseFrequencies(List.of(f.doubleValue())));
  }

  public void setVolume(DoubleProperty volume) {
    this.volume = volume;
  }

  private Vector2 generateChannels() throws InterruptedException {
    Shape shape = getCurrentShape();

    double length = shape.getLength();
    double drawingProgress = length == 0 ? 1 : lengthDrawn / length;
    Vector2 nextVector = applyEffects(count, shape.nextVector(drawingProgress));

    Vector2 channels = cutoff(nextVector);
    writeChannels((float) channels.getX(), (float) channels.getY());

    lengthDrawn += lengthIncrement;

    if (++count > MAX_COUNT) {
      count = 0;
    }

    if (lengthDrawn > length) {
      lengthDrawn = lengthDrawn - length;
      currentShape++;
    }

    if (currentShape >= frame.size()) {
      currentShape = 0;
      frame = frameQueue.take();
      lengthDrawn = 0;
      updateLengthIncrement();
    }

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
    int numNotes = sineEffects.size();
    vector = vector.scale(2 * mainFrequencyScale);
    double scaledVolume = (1 - mainFrequencyScale) / numNotes;
    for (SineEffect effect : sineEffects) {
      effect.setVolume(scaledVolume);
      vector = effect.apply(frame, vector);
    }
    for (Effect effect : effects.values()) {
      vector = effect.apply(frame, vector);
    }

    return vector.scale(volume.get());
  }

  private void setBaseFrequencies(List<Double> frequencies) {
    this.frequencies = frequencies;
    this.maxFrequency = frequencies.stream().max(Double::compareTo).get();
    octaveFrequency = maxFrequency * Math.pow(2, octave - 1);
    updateLengthIncrement();

    sineEffects.clear();
    for (Double frequency : frequencies) {
      if (frequency != maxFrequency) {
        sineEffects.add(new SineEffect(device.sampleRate(), frequency));
      }
    }
  }

  @Override
  public void setPitchBendFactor(double pitchBend) {
    this.pitchBend = pitchBend;
    updateLengthIncrement();
  }

  @Override
  public List<Double> getBaseFrequencies() {
    return frequencies;
  }

  @Override
  public List<Double> getFrequencies() {
    return frequencies.stream().map(d -> d * pitchBend).collect(Collectors.toList());
  }

  private Shape getCurrentShape() {
    if (frame.size() == 0) {
      return new Vector2();
    }

    return frame.get(currentShape);
  }

  private void updateLengthIncrement() {
    if (frame != null) {
      double totalLength = Shape.totalLength(frame);
      int sampleRate = device.sampleRate();
      double actualFrequency = octaveFrequency * pitchBend;
      lengthIncrement = Math.max(totalLength / (sampleRate / actualFrequency), MIN_LENGTH_INCREMENT);
    }
  }

  @Override
  public void run() {
    try {
      frame = frameQueue.take();
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
    octaveFrequency = maxFrequency * Math.pow(2, octave - 1);
  }

  @Override
  public void setMainFrequencyScale(double scale) {
    this.mainFrequencyScale = scale;
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

    AudioFormat audioFormat = new AudioFormat(device.sampleRate(), BITS_PER_SAMPLE, NUM_OUTPUTS, SIGNED, BIG_ENDIAN);

    return new AudioInputStream(new ByteArrayInputStream(input), audioFormat, framesRecorded);
  }

  private void playNotes(double noteVolume) {
    List<Double> frequencies = downKeys.stream().map(MidiNote::frequency).collect(Collectors.toList());
    double mainFrequency = frequencies.get(frequencies.size() - 1);
    frequency.set(mainFrequency);
    setBaseFrequencies(frequencies);
    volume.set(noteVolume);
  }

  @Override
  public void sendMidiMessage(ShortMessage message) {
    int command = message.getCommand();

    if (command == ShortMessage.NOTE_ON || command == ShortMessage.NOTE_OFF) {
      MidiNote note = new MidiNote(message.getData1());
      int velocity = message.getData2();

      double oldVolume = volume.get();
      double newVolume = velocity / 127.0;

      if (command == ShortMessage.NOTE_OFF) {
        downKeys.remove(note);
        if (downKeys.isEmpty()) {
          KeyValue kv = new KeyValue(volume, 0, Interpolator.EASE_OUT);
          KeyFrame kf = new KeyFrame(Duration.millis(500), kv);
          volumeTimeline = new Timeline(kf);
          Platform.runLater(volumeTimeline::play);
        } else {
          playNotes(oldVolume);
        }
      } else {
        downKeys.add(note);
        if (volumeTimeline != null) {
          volumeTimeline.stop();
          volumeTimeline = null;
        }
        playNotes(newVolume);
        KeyValue kv = new KeyValue(volume, volume.get() * 0.75, Interpolator.EASE_OUT);
        KeyFrame kf = new KeyFrame(Duration.millis(250), kv);
        volumeTimeline = new Timeline(kf);
        Platform.runLater(volumeTimeline::play);
      }
    } else if (command == ShortMessage.PITCH_BEND) {
      // using these instructions https://sites.uci.edu/camp2014/2014/04/30/managing-midi-pitchbend-messages/

      int pitchBend = (message.getData2() << PITCH_BEND_DATA_LENGTH) | message.getData1();
      // get pitch bend in range -1 to 1
      double pitchBendFactor = (double) pitchBend / PITCH_BEND_MAX;
      pitchBendFactor = 2 * pitchBendFactor - 1;
      pitchBendFactor *= PITCH_BEND_SEMITONES;
      // 12 tone equal temperament
      pitchBendFactor /= 12;
      pitchBendFactor = Math.pow(2, pitchBendFactor);

      setPitchBendFactor(pitchBendFactor);
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
