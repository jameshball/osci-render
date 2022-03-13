package sh.ball.gui.controller;

import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.control.TextField;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.effect.RotateEffect;
import sh.ball.audio.effect.TranslateEffect;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.midi.MidiNote;
import sh.ball.shapes.Vector2;

import java.net.URL;
import java.text.DecimalFormat;
import java.text.DecimalFormatSymbols;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.function.Consumer;

import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.math.Math.tryParse;

public class ImageController implements Initializable, SubController {

  private static final double MAX_FREQUENCY = 12000;
  private static final int DEFAULT_SAMPLE_RATE = 192000;
  private static final double SCROLL_DELTA = 0.05;

  private final RotateEffect rotateEffect;
  private final TranslateEffect translateEffect;

  private final DoubleProperty frequency;

  @FXML
  private TextField translationXTextField;
  @FXML
  private TextField translationYTextField;
  @FXML
  private CheckBox translateEllipseCheckBox;
  @FXML
  private CheckBox translateCheckBox;
  @FXML
  private CheckBox translationScaleMic;
  @FXML
  private Slider translationScaleSlider;
  @FXML
  private SVGPath translationScaleMidi;
  @FXML
  private Slider frequencySlider;
  @FXML
  private SVGPath frequencyMidi;
  @FXML
  private CheckBox frequencyMic;
  @FXML
  private Slider rotateSpeedSlider;
  @FXML
  private SVGPath rotateSpeedMidi;
  @FXML
  private CheckBox rotateSpeedMic;
  @FXML
  private Slider translationSpeedSlider;
  @FXML
  private SVGPath translationSpeedMidi;
  @FXML
  private CheckBox translationSpeedMic;
  @FXML
  private Slider volumeSlider;
  @FXML
  private SVGPath volumeMidi;
  @FXML
  private CheckBox volumeMic;
  @FXML
  private Slider visibilitySlider;
  @FXML
  private SVGPath visibilityMidi;
  @FXML
  private CheckBox visibilityMic;
  @FXML
  private Button resetRotationButton;

  public ImageController() {
    this.frequency = new SimpleDoubleProperty(0);
    this.rotateEffect = new RotateEffect(DEFAULT_SAMPLE_RATE);
    this.translateEffect = new TranslateEffect(DEFAULT_SAMPLE_RATE, 1, new Vector2());
  }

  public void setAudioDevice(AudioDevice device) {
    int sampleRate = device.sampleRate();
    rotateEffect.setSampleRate(sampleRate);
    translateEffect.setSampleRate(sampleRate);
  }

  public void setTranslation(Vector2 translation) {
    translationXTextField.setText(MainController.FORMAT.format(translation.getX()));
    translationYTextField.setText(MainController.FORMAT.format(translation.getY()));
  }

  // changes the sinusoidal translation of the image rendered
  private void updateTranslation() {
    translateEffect.setTranslation(new Vector2(
      tryParse(translationXTextField.getText()),
      tryParse(translationYTextField.getText())
    ));
  }

  private void changeTranslation(boolean increase, TextField field) {
    double old = tryParse(field.getText());
    double delta = increase ? SCROLL_DELTA : -SCROLL_DELTA;
    field.setText(MainController.FORMAT.format(old + delta));
  }

  public boolean mouseTranslate() {
    return translateCheckBox.isSelected();
  }

  public void disableMouseTranslate() {
    translateCheckBox.setSelected(false);
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    audioPlayer.addEffect(EffectType.ROTATE, rotateEffect);
    audioPlayer.addEffect(EffectType.TRANSLATE, translateEffect);

    Map<Slider, Consumer<Double>> sliderMap = Map.of(
      rotateSpeedSlider, rotateEffect::setSpeed,
      translationSpeedSlider, translateEffect::setSpeed,
      visibilitySlider, audioPlayer::setBaseFrequencyVolumeScale
    );
    sliderMap.keySet().forEach(slider ->
      slider.valueProperty().addListener((source, oldValue, newValue) ->
        sliderMap.get(slider).accept(newValue.doubleValue())
      )
    );

    resetRotationButton.setOnAction(e -> rotateEffect.resetTheta());

    translationXTextField.textProperty().addListener(e -> updateTranslation());
    translationXTextField.setOnScroll((e) -> changeTranslation(e.getDeltaY() > 0, translationXTextField));
    translationYTextField.textProperty().addListener(e -> updateTranslation());
    translationYTextField.setOnScroll((e) -> changeTranslation(e.getDeltaY() > 0, translationYTextField));

    translateEllipseCheckBox.selectedProperty().addListener((e, old, ellipse) -> translateEffect.setEllipse(ellipse));

    translationScaleSlider.valueProperty().addListener((e, old, scale) -> translateEffect.setScale(scale.doubleValue()));

    // converts the value of frequencySlider to the actual frequency that it represents so that it
    // can increase at an exponential scale.
    frequencySlider.valueProperty().addListener((o, old, f) -> frequency.set(Math.pow(MAX_FREQUENCY, f.doubleValue())));
    frequency.addListener((o, old, f) -> frequencySlider.setValue(Math.log(f.doubleValue()) / Math.log(MAX_FREQUENCY)));
    audioPlayer.setFrequency(frequency);
    // default value is middle C
    frequency.set(MidiNote.MIDDLE_C);
    volumeSlider.valueProperty().addListener((e, old, value) -> {
      audioPlayer.setVolume(value.doubleValue() / 3.0);
    });
    volumeSlider.valueProperty().addListener(e -> {
      if (!audioPlayer.midiPlaying()) {
        audioPlayer.resetMidi();
      }
    });
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      frequencyMidi, frequencySlider,
      rotateSpeedMidi, rotateSpeedSlider,
      translationSpeedMidi, translationSpeedSlider,
      volumeMidi, volumeSlider,
      visibilityMidi, visibilitySlider,
      translationScaleMidi, translationScaleSlider
    );
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    return List.of(frequencyMic, rotateSpeedMic, translationSpeedMic, volumeMic,
      visibilityMic, translationScaleMic);
  }

  @Override
  public List<Slider> sliders() {
    return List.of(frequencySlider, rotateSpeedSlider, translationSpeedSlider,
      volumeSlider, visibilitySlider, translationScaleSlider);
  }

  @Override
  public List<String> labels() {
    return List.of("frequency", "rotateSpeed", "translationSpeed", "volume",
      "visibility", "translationScale");
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("translation");
    Element x = document.createElement("x");
    x.appendChild(document.createTextNode(translationXTextField.getText()));
    Element y = document.createElement("y");
    y.appendChild(document.createTextNode(translationYTextField.getText()));
    Element ellipse = document.createElement("ellipse");
    ellipse.appendChild(document.createTextNode(Boolean.toString(translateEllipseCheckBox.isSelected())));
    element.appendChild(x);
    element.appendChild(y);
    element.appendChild(ellipse);
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("translation").item(0);
    Element x = (Element) element.getElementsByTagName("x").item(0);
    Element y = (Element) element.getElementsByTagName("y").item(0);
    translationXTextField.setText(x.getTextContent());
    translationYTextField.setText(y.getTextContent());

    // For backwards compatibility we assume a default value
    Element ellipse = (Element) element.getElementsByTagName("ellipse").item(0);
    translateEllipseCheckBox.setSelected(ellipse != null && Boolean.parseBoolean(ellipse.getTextContent()));
  }
}
