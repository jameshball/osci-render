package sh.ball.audio.midi;

import javax.sound.midi.*;

import java.util.ArrayList;
import java.util.List;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class MidiCommunicator implements Runnable {

  private final List<MidiDevice> devices = new ArrayList<>();
  private final List<MidiListener> listeners = new ArrayList<>();

  @Override
  public void run() {
    MidiDevice.Info[] infos = MidiSystem.getMidiDeviceInfo();
    for (MidiDevice.Info info : infos) {
      try {
        MidiDevice device = MidiSystem.getMidiDevice(info);
        List<Transmitter> transmitters = device.getTransmitters();

        transmitters.forEach(t -> t.setReceiver(
          new MidiInputReceiver(this)
        ));

        Transmitter trans = device.getTransmitter();
        trans.setReceiver(new MidiInputReceiver(this));

        device.open();
        devices.add(device);

      } catch (MidiUnavailableException ignored) {}
    }
  }

  public void addListener(MidiListener listener) {
    listeners.add(listener);
  }

  private void notifyListeners(ShortMessage message) {
    for (MidiListener listener : listeners) {
      listener.sendMidiMessage(message);
    }
  }

  public void stop() {
    devices.forEach(MidiDevice::close);
  }

  private record MidiInputReceiver(
    MidiCommunicator communicator) implements Receiver {

    public void send(MidiMessage message, long timeStamp) {
      if (message instanceof ShortMessage shortMessage)
      communicator.notifyListeners(shortMessage);
    }

    public void close() {
    }
  }
}