package sh.ball.gui.controller;

import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
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
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.function.Consumer;

import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.math.Math.tryParse;

public class ImageController implements Initializable, SubController {

  private static final double MAX_FREQUENCY = 12000;
  private static final int DEFAULT_SAMPLE_RATE = 192000;

  private final RotateEffect rotateEffect;
  private final TranslateEffect translateEffect;

  private final DoubleProperty frequency;

  @FXML
  private TextField translationXTextField;
  @FXML
  private TextField translationYTextField;
  @FXML
  private Slider frequencySlider;
  @FXML
  private SVGPath frequencyMidi;
  @FXML
  private Slider rotateSpeedSlider;
  @FXML
  private SVGPath rotateSpeedMidi;
  @FXML
  private Slider translationSpeedSlider;
  @FXML
  private SVGPath translationSpeedMidi;
  @FXML
  private Slider volumeSlider;
  @FXML
  private SVGPath volumeMidi;
  @FXML
  private Slider visibilitySlider;
  @FXML
  private SVGPath visibilityMidi;

  public ImageController() {
    this.frequency = new SimpleDoubleProperty(0);
    this.rotateEffect = new RotateEffect(DEFAULT_SAMPLE_RATE);
    this.translateEffect = new TranslateEffect(DEFAULT_SAMPLE_RATE);
  }

  public void setAudioDevice(AudioDevice device) {
    int sampleRate = device.sampleRate();
    rotateEffect.setSampleRate(sampleRate);
    translateEffect.setSampleRate(sampleRate);
  }

  // changes the sinusoidal translation of the image rendered
  private void updateTranslation() {
    translateEffect.setTranslation(new Vector2(
      tryParse(translationXTextField.getText()),
      tryParse(translationYTextField.getText())
    ));
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    audioPlayer.addEffect(EffectType.ROTATE, rotateEffect);
    audioPlayer.addEffect(EffectType.TRANSLATE, translateEffect);

    Map<Slider, Consumer<Double>> sliderMap = Map.of(
      rotateSpeedSlider, rotateEffect::setSpeed,
      translationSpeedSlider, translateEffect::setSpeed,
      visibilitySlider, audioPlayer::setMainFrequencyScale
    );
    sliderMap.keySet().forEach(slider ->
      slider.valueProperty().addListener((source, oldValue, newValue) ->
        sliderMap.get(slider).accept(newValue.doubleValue())
      )
    );

    translationXTextField.textProperty().addListener(e -> updateTranslation());
    translationYTextField.textProperty().addListener(e -> updateTranslation());

    // converts the value of frequencySlider to the actual frequency that it represents so that it
    // can increase at an exponential scale.
    frequencySlider.valueProperty().addListener((o, old, f) -> frequency.set(Math.pow(MAX_FREQUENCY, f.doubleValue())));
    frequency.addListener((o, old, f) -> frequencySlider.setValue(Math.log(f.doubleValue()) / Math.log(MAX_FREQUENCY)));
    audioPlayer.setFrequency(frequency);
    // default value is middle C
    frequency.set(MidiNote.MIDDLE_C);
    audioPlayer.setVolume(volumeSlider.valueProperty());
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      frequencyMidi, frequencySlider,
      rotateSpeedMidi, rotateSpeedSlider,
      translationSpeedMidi, translationSpeedSlider,
      volumeMidi, volumeSlider,
      visibilityMidi, visibilitySlider
    );
  }

  @Override
  public List<Slider> sliders() {
    return List.of(frequencySlider, rotateSpeedSlider, translationSpeedSlider,
      volumeSlider, visibilitySlider);
  }

  @Override
  public List<String> labels() {
    return List.of("frequency", "rotateSpeed", "translationSpeed", "volume",
      "visibility");
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("translation");
    Element x = document.createElement("x");
    x.appendChild(document.createTextNode(translationXTextField.getText()));
    Element y = document.createElement("y");
    y.appendChild(document.createTextNode(translationYTextField.getText()));
    element.appendChild(x);
    element.appendChild(y);
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("translation").item(0);
    Element x = (Element) element.getElementsByTagName("x").item(0);
    Element y = (Element) element.getElementsByTagName("y").item(0);
    translationXTextField.setText(x.getTextContent());
    translationYTextField.setText(y.getTextContent());
  }
}
