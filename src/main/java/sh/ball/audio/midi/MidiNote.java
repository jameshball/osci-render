package sh.ball.audio.midi;

import java.util.Objects;

public class MidiNote {

  private static final String[] NOTE_NAMES = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

  private final String name;
  private final int key;
  private final int octave;

  public MidiNote(int key) {
    this.key = key;
    this.octave = (key / 12)-1;
    int note = key % 12;
    this.name = NOTE_NAMES[note];
  }

  public int key() {
    return key;
  }

  @Override
  public boolean equals(Object o) {
    if (this == o) return true;
    if (o == null || getClass() != o.getClass()) return false;
    MidiNote midiNote = (MidiNote) o;
    return key == midiNote.key;
  }

  @Override
  public int hashCode() {
    return Objects.hash(key);
  }

  @Override
  public String toString() {
    return "Note -> " + this.name + this.octave + " key=" + this.key;
  }
}
