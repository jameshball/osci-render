package sh.ball.audio.midi;

import java.util.List;

public class MidiNote {

  public static final int MAX_VELOCITY = 127;
  public static final short MAX_CC = 0x78;
  public static final short MAX_CHANNEL = 15;
  public static final short NUM_CHANNELS = (short) (MAX_CHANNEL + 1);
  public static final short NUM_KEYS = 128;
  public static final short ALL_SOUND_OFF = 120;
  public static final short ALL_NOTES_OFF = 123;
  public static final double MIDDLE_C = 261.6255798;
  public static final int PITCH_BEND_DATA_LENGTH = 7;
  public static final int PITCH_BEND_MAX = 16383;
  public static final int PITCH_BEND_SEMITONES = 2;
  public static final int BANK_SELECT_MSB = 0;
  public static final int MODULATION_WHEEL_MSB = 1;
  public static final int FOOT_PEDAL_MSB = 4;
  public static final int DATA_ENTRY_MSB = 6;
  public static final int VOLUME_MSB = 7;
  public static final int PAN_MSB = 10;
  public static final int EXPRESSION_MSB = 11;
  public static final int BANK_SELECT_LSB = 32;
  public static final int MODULATION_WHEEL_LSB = 33;
  public static final int FOOT_PEDAL_LSB = 36;
  public static final int DATA_ENTRY_LSB = 38;
  public static final int VOLUME_LSB = 39;
  public static final int PAN_LSB = 42;
  public static final int EXPRESSION_LSB = 43;
  public static final int NON_REGISTERED_PARAMETER_LSB = 98;
  public static final int NON_REGISTERED_PARAMETER_MSB = 99;
  public static final int REGISTERED_PARAMETER_LSB = 100;
  public static final int REGISTERED_PARAMETER_MSB = 101;
  public static final int PITCH_BEND_RANGE_RPM_LSB = 0;
  public static final int PITCH_BEND_RANGE_RPM_MSB = 0;
  public static final List<Integer> RESERVED_CC = List.of(
    BANK_SELECT_MSB,
    MODULATION_WHEEL_MSB,
    FOOT_PEDAL_MSB,
    DATA_ENTRY_MSB,
    VOLUME_MSB,
    PAN_MSB,
    EXPRESSION_MSB,
    BANK_SELECT_LSB,
    MODULATION_WHEEL_LSB,
    FOOT_PEDAL_LSB,
    DATA_ENTRY_LSB,
    VOLUME_LSB,
    PAN_LSB,
    EXPRESSION_LSB,
    NON_REGISTERED_PARAMETER_LSB,
    NON_REGISTERED_PARAMETER_MSB,
    REGISTERED_PARAMETER_LSB,
    REGISTERED_PARAMETER_MSB
  );

  // Concert A Pitch is A4 and has the key number 69
  final static int KEY_A4 = 69;
  public static final double A4 = 440;

  private static final String[] NOTE_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

  private final String name;
  private final int key;
  private final int octave;
  private final int channel;

  // pre-calculated map from midi key to frequency
  public static final double[] KEY_TO_FREQUENCY = new double[NUM_KEYS];

  static {
    for (int i = 0; i < NUM_KEYS; i++) {
      KEY_TO_FREQUENCY[i] = Math.pow(2, (i - KEY_A4) / 12.0) * A4;
    }
  }

  public MidiNote(int key, int channel) {
    this.key = key;
    this.channel = channel;
    this.octave = (key / 12)-1;
    int note = key % 12;
    this.name = NOTE_NAMES[note];
  }

  public MidiNote(int key) {
    this(key, 0);
  }

  public int key() {
    return key;
  }

  public double frequency() {
    // Returns the frequency of the given key (equal temperament)
    return KEY_TO_FREQUENCY[key];
  }

  public int channel() {
    return channel;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    MidiNote midiNote = (MidiNote) o;
    return key == midiNote.key && channel == midiNote.channel;
  }

  @Override
  public int hashCode() {
    return (key << 16) + channel;
  }

  @Override
  public String toString() {
    return "Note -> " + this.name + this.octave + " key=" + this.key;
  }
}
