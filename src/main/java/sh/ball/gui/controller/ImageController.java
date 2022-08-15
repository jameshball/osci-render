package sh.ball.gui.controller;

import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.midi.MidiNote;

import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;

import static sh.ball.gui.Gui.audioPlayer;

public class ImageController implements Initializable, SubController {

  private static final double MAX_FREQUENCY = 12000;

  @FXML
  private Slider frequencySlider;
  @FXML
  private Spinner<Double> frequencySpinner;
  @FXML
  private SVGPath frequencyMidi;
  @FXML
  private CheckBox frequencyMic;

  public ImageController() {}

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    frequencySpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0, MAX_FREQUENCY, MidiNote.MIDDLE_C, 1));

    frequencySpinner.valueProperty().addListener((o, old, f) -> {
      frequencySlider.setValue(Math.log(f) / Math.log(MAX_FREQUENCY));
      audioPlayer.setFrequency(f);
    });

    frequencySlider.valueProperty().addListener((o, old, f) -> {
      double frequency = Math.pow(MAX_FREQUENCY, f.doubleValue());
      frequencySpinner.getValueFactory().setValue(frequency);
      audioPlayer.setFrequency(frequency);
    });

    // default value is middle C
    audioPlayer.setFrequency(MidiNote.MIDDLE_C);
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      frequencyMidi, frequencySlider
    );
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return List.of(frequencyMic.selectedProperty());
  }

  @Override
  public List<Slider> sliders() {
    return List.of(frequencySlider);
  }

  @Override
  public List<String> labels() {
    return List.of("frequency");
  }

  @Override
  public List<Element> save(Document document) {
    return List.of(document.createElement("null"));
  }

  @Override
  public void load(Element root) {}

  @Override
  public void micNotAvailable() {}
}
