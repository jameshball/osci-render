package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.InvalidationListener;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import javafx.util.Duration;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.FrequencyAnalyser;
import sh.ball.audio.effect.*;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.shapes.Shape;

import java.net.URL;
import java.util.*;

import static sh.ball.gui.Gui.audioPlayer;

public class EffectsController implements Initializable, SubController {

  private static final int DEFAULT_SAMPLE_RATE = 192000;

  private Map<EffectType, Slider> effectTypes;

  private final WobbleEffect wobbleEffect;
  private final EffectAnimator wobbleAnimator;
  private final EffectAnimator traceMinAnimator;
  private final EffectAnimator traceMaxAnimator;
  private final EffectAnimator vectorCancellingAnimator;
  private final EffectAnimator bitCrushAnimator;
  private final EffectAnimator smoothingAnimator;
  private final EffectAnimator verticalDistortAnimator;
  private final EffectAnimator horizontalDistortAnimator;

  @FXML
  private CheckBox vectorCancellingCheckBox;
  @FXML
  private Slider vectorCancellingSlider;
  @FXML
  private SVGPath vectorCancellingMidi;
  @FXML
  private ComboBox<AnimationType> vectorCancellingComboBox;
  @FXML
  private CheckBox vectorCancellingMic;
  @FXML
  private CheckBox bitCrushCheckBox;
  @FXML
  private Slider bitCrushSlider;
  @FXML
  private SVGPath bitCrushMidi;
  @FXML
  private ComboBox<AnimationType> bitCrushComboBox;
  @FXML
  private CheckBox bitCrushMic;
  @FXML
  private CheckBox verticalDistortCheckBox;
  @FXML
  private Slider verticalDistortSlider;
  @FXML
  private SVGPath verticalDistortMidi;
  @FXML
  private ComboBox<AnimationType> verticalDistortComboBox;
  @FXML
  private CheckBox verticalDistortMic;
  @FXML
  private CheckBox horizontalDistortCheckBox;
  @FXML
  private Slider horizontalDistortSlider;
  @FXML
  private SVGPath horizontalDistortMidi;
  @FXML
  private ComboBox<AnimationType> horizontalDistortComboBox;
  @FXML
  private CheckBox horizontalDistortMic;
  @FXML
  private CheckBox wobbleCheckBox;
  @FXML
  private Slider wobbleSlider;
  @FXML
  private SVGPath wobbleMidi;
  @FXML
  private ComboBox<AnimationType> wobbleComboBox;
  @FXML
  private CheckBox wobbleMic;
  @FXML
  private CheckBox smoothingCheckBox;
  @FXML
  private Slider smoothingSlider;
  @FXML
  private SVGPath smoothingMidi;
  @FXML
  private ComboBox<AnimationType> smoothingComboBox;
  @FXML
  private CheckBox smoothingMic;
  @FXML
  private CheckBox traceMinCheckBox;
  @FXML
  private Slider traceMinSlider;
  @FXML
  private SVGPath traceMinMidi;
  @FXML
  private ComboBox<AnimationType> traceMinComboBox;
  @FXML
  private CheckBox traceMinMic;
  @FXML
  private CheckBox traceMaxCheckBox;
  @FXML
  private Slider traceMaxSlider;
  @FXML
  private SVGPath traceMaxMidi;
  @FXML
  private ComboBox<AnimationType> traceMaxComboBox;
  @FXML
  private CheckBox traceMaxMic;

  public EffectsController() {
    this.wobbleEffect = new WobbleEffect(DEFAULT_SAMPLE_RATE);
    this.wobbleAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, wobbleEffect);
    this.traceMinAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMin));
    this.traceMaxAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMax));
    this.vectorCancellingAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new VectorCancellingEffect());
    this.bitCrushAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new BitCrushEffect());
    this.smoothingAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new SmoothEffect(1));
    this.verticalDistortAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new VerticalDistortEffect(0.2));
    this.horizontalDistortAnimator = new EffectAnimator(DEFAULT_SAMPLE_RATE, new HorizontalDistortEffect(0.2));
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      vectorCancellingMidi, vectorCancellingSlider,
      bitCrushMidi, bitCrushSlider,
      wobbleMidi, wobbleSlider,
      smoothingMidi, smoothingSlider,
      traceMinMidi, traceMinSlider,
      traceMaxMidi, traceMaxSlider,
      verticalDistortMidi, verticalDistortSlider,
      horizontalDistortMidi, horizontalDistortSlider
    );
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return Map.of(
      vectorCancellingComboBox, vectorCancellingAnimator,
      bitCrushComboBox, bitCrushAnimator,
      wobbleComboBox, wobbleAnimator,
      smoothingComboBox, smoothingAnimator,
      traceMinComboBox, traceMinAnimator,
      traceMaxComboBox, traceMaxAnimator,
      verticalDistortComboBox, verticalDistortAnimator,
      horizontalDistortComboBox, horizontalDistortAnimator
    );
  }

  // Maps EffectTypes to the slider that controls the effect so that they can be
  // toggled when the appropriate checkbox is ticked.
  private void initializeEffectTypes() {
    effectTypes = Map.of(
      EffectType.VECTOR_CANCELLING,
      vectorCancellingSlider,
      EffectType.BIT_CRUSH,
      bitCrushSlider,
      EffectType.VERTICAL_DISTORT,
      verticalDistortSlider,
      EffectType.HORIZONTAL_DISTORT,
      horizontalDistortSlider,
      EffectType.WOBBLE,
      wobbleSlider,
      EffectType.SMOOTH,
      smoothingSlider,
      EffectType.TRACE_MIN,
      traceMinSlider,
      EffectType.TRACE_MAX,
      traceMaxSlider
    );
  }

  // selects or deselects the given audio effect
  public void updateEffect(EffectType type, boolean checked, SettableEffect effect, double value) {
    if (checked) {
      effect.setValue(value);
      audioPlayer.addEffect(type, effect);
      effectTypes.get(type).setDisable(false);
    } else {
      audioPlayer.removeEffect(type);
      effectTypes.get(type).setDisable(true);
    }
  }

  public void setAudioDevice(AudioDevice device) {
    wobbleEffect.setSampleRate(device.sampleRate());
    Map<ComboBox<AnimationType>, EffectAnimator> comboAnimatorMap = getComboBoxAnimatorMap();
    for (EffectAnimator animator : comboAnimatorMap.values()) {
      animator.setSampleRate(device.sampleRate());
    }
    restartEffects();
  }

  public void setFrequencyAnalyser(FrequencyAnalyser<List<Shape>> analyser) {
    analyser.addListener(wobbleEffect);
  }

  public void restartEffects() {
    // apply the wobble effect after a second as the frequency of the audio takes a while to
    // propagate and send to its listeners.
    KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> wobbleAnimator.setValue(0));
    KeyFrame kf2 = new KeyFrame(Duration.seconds(1), e -> {
      wobbleEffect.update();
      wobbleAnimator.setValue(wobbleSlider.getValue());
    });
    Timeline timeline = new Timeline(kf1, kf2);
    Platform.runLater(timeline::play);
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    initializeEffectTypes();

    InvalidationListener bitCrushListener = e ->
      updateEffect(EffectType.BIT_CRUSH, bitCrushCheckBox.isSelected(), bitCrushAnimator, bitCrushSlider.getValue());
    InvalidationListener verticalDistortListener = e ->
      updateEffect(EffectType.VERTICAL_DISTORT, verticalDistortCheckBox.isSelected(), verticalDistortAnimator, verticalDistortSlider.getValue());
    InvalidationListener horizontalDistortListener = e ->
      updateEffect(EffectType.HORIZONTAL_DISTORT, horizontalDistortCheckBox.isSelected(), horizontalDistortAnimator, horizontalDistortSlider.getValue());
    InvalidationListener wobbleListener = e ->
      updateEffect(EffectType.WOBBLE, wobbleCheckBox.isSelected(), wobbleAnimator, wobbleSlider.getValue());
    InvalidationListener smoothListener = e ->
      updateEffect(EffectType.SMOOTH, smoothingCheckBox.isSelected(), smoothingAnimator, smoothingSlider.getValue());
    InvalidationListener vectorCancellingListener = e ->
      updateEffect(EffectType.VECTOR_CANCELLING, vectorCancellingCheckBox.isSelected(), vectorCancellingAnimator, vectorCancellingSlider.getValue());
    InvalidationListener traceMinListener = e ->
      updateEffect(EffectType.TRACE_MIN, traceMinCheckBox.isSelected(), traceMinAnimator, traceMinSlider.getValue());
    InvalidationListener traceMaxListener = e ->
      updateEffect(EffectType.TRACE_MAX, traceMaxCheckBox.isSelected(), traceMaxAnimator, traceMaxSlider.getValue());

    vectorCancellingSlider.valueProperty().addListener(vectorCancellingListener);
    vectorCancellingCheckBox.selectedProperty().addListener(vectorCancellingListener);

    bitCrushSlider.valueProperty().addListener(bitCrushListener);
    bitCrushCheckBox.selectedProperty().addListener(bitCrushListener);

    verticalDistortSlider.valueProperty().addListener(verticalDistortListener);
    verticalDistortCheckBox.selectedProperty().addListener(verticalDistortListener);

    horizontalDistortSlider.valueProperty().addListener(horizontalDistortListener);
    horizontalDistortCheckBox.selectedProperty().addListener(horizontalDistortListener);

    wobbleSlider.valueProperty().addListener(wobbleListener);
    wobbleCheckBox.selectedProperty().addListener(wobbleListener);
    wobbleCheckBox.selectedProperty().addListener(e -> wobbleEffect.update());

    smoothingSlider.valueProperty().addListener(smoothListener);
    smoothingCheckBox.selectedProperty().addListener(smoothListener);

    traceMinSlider.valueProperty().addListener(traceMinListener);
    traceMinCheckBox.selectedProperty().addListener(traceMinListener);
    traceMinCheckBox.selectedProperty().addListener((e, old, selected) -> {
      if (selected) {
        traceMinAnimator.setValue(traceMinSlider.getValue());
      } else {
        traceMinAnimator.setValue(0.0);
      }
    });

    traceMaxSlider.valueProperty().addListener(traceMaxListener);
    traceMaxCheckBox.selectedProperty().addListener(traceMaxListener);
    traceMaxCheckBox.selectedProperty().addListener((e, old, selected) -> {
      if (selected) {
        traceMaxAnimator.setValue(traceMaxSlider.getValue());
      } else {
        traceMaxAnimator.setValue(1.0);
      }
    });

    List<AnimationType> animations = List.of(AnimationType.STATIC, AnimationType.SEESAW, AnimationType.LINEAR, AnimationType.FORWARD, AnimationType.REVERSE);
    Map<ComboBox<AnimationType>, EffectAnimator> comboAnimatorMap = getComboBoxAnimatorMap();
    for (Map.Entry<ComboBox<AnimationType>, EffectAnimator> entry : comboAnimatorMap.entrySet()) {
      entry.getKey().setItems(FXCollections.observableList(animations));
      entry.getKey().setValue(AnimationType.STATIC);
      entry.getKey().valueProperty().addListener((options, old, animationType) -> {
        entry.getValue().setAnimation(animationType);
      });
    }

    List<EffectAnimator> animators = animators();
    List<Slider> sliders = sliders();
    for (int i = 0; i < sliders.size(); i++) {
      int finalI = i;
      sliders.get(i).minProperty().addListener((e, old, min) ->
        animators.get(finalI).setMin(min.doubleValue()));
      sliders.get(i).maxProperty().addListener((e, old, max) ->
        animators.get(finalI).setMax(max.doubleValue()));
    }
  }

  private List<ComboBox<AnimationType>> comboBoxes() {
    return getComboBoxAnimatorMap().keySet().stream().toList();
  }
  private List<CheckBox> checkBoxes() {
    return List.of(vectorCancellingCheckBox, bitCrushCheckBox, verticalDistortCheckBox,
      horizontalDistortCheckBox, wobbleCheckBox, smoothingCheckBox, traceMinCheckBox,
      traceMaxCheckBox);
  }
  private List<EffectAnimator> animators() {
    return List.of(vectorCancellingAnimator, bitCrushAnimator, verticalDistortAnimator,
      horizontalDistortAnimator, wobbleAnimator, smoothingAnimator, traceMinAnimator,
      traceMaxAnimator);
  }
  @Override
  public List<CheckBox> micCheckBoxes() {
    return List.of(vectorCancellingMic, bitCrushMic, verticalDistortMic, horizontalDistortMic,
      wobbleMic, smoothingMic, traceMinMic, traceMaxMic);
  }
  @Override
  public List<Slider> sliders() {
    return List.of(vectorCancellingSlider, bitCrushSlider, verticalDistortSlider,
      horizontalDistortSlider, wobbleSlider, smoothingSlider, traceMinSlider, traceMaxSlider);
  }
  @Override
  public List<String> labels() {
    return List.of("vectorCancelling", "bitCrush", "verticalDistort", "horizontalDistort",
      "wobble", "smooth", "traceMin", "traceMax");
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("checkBoxes");
    List<ComboBox<AnimationType>> comboBoxes = comboBoxes();
    List<CheckBox> checkBoxes = checkBoxes();
    List<String> labels = labels();
    for (int i = 0; i < checkBoxes.size(); i++) {
      Element checkBox = document.createElement(labels.get(i));
      Element selected = document.createElement("selected");
      selected.appendChild(
        document.createTextNode(checkBoxes.get(i).selectedProperty().getValue().toString())
      );
      Element animation = document.createElement("animation");
      animation.appendChild(document.createTextNode(comboBoxes.get(i).getValue().toString()));
      checkBox.appendChild(selected);
      checkBox.appendChild(animation);
      element.appendChild(checkBox);
    }
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("checkBoxes").item(0);
    List<ComboBox<AnimationType>> comboBoxes = comboBoxes();
    List<CheckBox> checkBoxes = checkBoxes();
    List<String> labels = labels();
    for (int i = 0; i < checkBoxes.size(); i++) {
      Element checkBox = (Element) element.getElementsByTagName(labels.get(i)).item(0);
      String selected;
      // backwards compatibility
      if (checkBox.getElementsByTagName("selected").getLength() > 0) {
        selected = checkBox.getElementsByTagName("selected").item(0).getTextContent();
        if (checkBox.getElementsByTagName("animation").getLength() > 0) {
          String animation = checkBox.getElementsByTagName("animation").item(0).getTextContent();
          comboBoxes.get(i).setValue(AnimationType.fromString(animation));
        }
      } else {
        selected = checkBox.getTextContent();
        comboBoxes.get(i).setValue(AnimationType.STATIC);
      }
      checkBoxes.get(i).setSelected(Boolean.parseBoolean(selected));
    }
  }
}
