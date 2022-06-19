package sh.ball.gui.controller;

import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.input.KeyCode;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.TranslateEffect;
import sh.ball.audio.midi.MidiNote;

import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;

import static sh.ball.gui.Gui.audioPlayer;

public class ImageController implements Initializable, SubController {

  private static final double MAX_FREQUENCY = 12000;
  private final DoubleProperty frequency;

  @FXML
  private Slider frequencySlider;
  @FXML
  private Spinner<Double> frequencySpinner;
  @FXML
  private SVGPath frequencyMidi;
  @FXML
  private CheckBox frequencyMic;

  public ImageController() {
    this.frequency = new SimpleDoubleProperty(0);
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    frequencySpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0, MAX_FREQUENCY, MidiNote.MIDDLE_C, 1));
    frequencySpinner.valueProperty().addListener((o, old, f) -> frequency.set(f));

    frequency.addListener((o, old, f) -> {
      frequencySlider.setValue(Math.log(f.doubleValue()) / Math.log(MAX_FREQUENCY));
      frequencySpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0, MAX_FREQUENCY, f.doubleValue(), 1));
    });
    audioPlayer.setFrequency(frequency);
    // default value is middle C
    frequency.set(MidiNote.MIDDLE_C);
    frequencySlider.setOnKeyPressed(e -> {
      if (e.getCode() == KeyCode.LEFT || e.getCode() == KeyCode.RIGHT) {
        slidersUpdated();
      }
    });
    frequencySlider.setOnMouseDragged(e -> slidersUpdated());
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      frequencyMidi, frequencySlider
    );
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    return List.of(frequencyMic);
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
  public void load(Element root) {
    slidersUpdated();
  }

  @Override
  public void slidersUpdated() {
    frequency.set(Math.pow(MAX_FREQUENCY, frequencySlider.getValue()));
  }
}
