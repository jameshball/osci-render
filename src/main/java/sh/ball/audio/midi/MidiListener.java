package sh.ball.audio.midi;

public interface MidiListener {

  void sendMidiMessage(int status, MidiNote note, int pressure);
}
