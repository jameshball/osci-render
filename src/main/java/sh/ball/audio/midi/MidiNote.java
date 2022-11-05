package sh.ball.audio.midi;

public class MidiNote {

  public static final int MAX_VELOCITY = 127;
  public static final short MAX_CC = 0x78;
  public static final short MAX_CHANNEL = 15;
  public static final short NUM_CHANNELS = (short) (MAX_CHANNEL + 1);
  public static final short NUM_KEYS = 128;
  public static final short ALL_NOTES_OFF = 0x7B;
  public static final double MIDDLE_C = 261.6255798;
  public static final int PITCH_BEND_DATA_LENGTH = 7;
  public static final int PITCH_BEND_MAX = 16383;
  public static final int PITCH_BEND_SEMITONES = 2;

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
