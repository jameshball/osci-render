package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
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
import sh.ball.gui.components.EffectComponentGroup;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.net.URL;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;

import static sh.ball.gui.Gui.audioPlayer;

public class EffectsController implements Initializable, SubController {

  private static final int DEFAULT_SAMPLE_RATE = 192000;

  private final WobbleEffect wobbleEffect;
  private final PerspectiveEffect perspectiveEffect;
  private final TranslateEffect translateEffect;
  private final RotateEffect rotateEffect;

  @FXML
  private EffectComponentGroup vectorCancelling;
  @FXML
  private EffectComponentGroup bitCrush;
  @FXML
  private EffectComponentGroup verticalDistort;
  @FXML
  private EffectComponentGroup horizontalDistort;
  @FXML
  private EffectComponentGroup wobble;
  @FXML
  private EffectComponentGroup smoothing;
  @FXML
  private EffectComponentGroup traceMax;
  @FXML
  private EffectComponentGroup traceMin;
  @FXML
  private EffectComponentGroup perspective;
  @FXML
  private EffectComponentGroup translationScale;
  @FXML
  private EffectComponentGroup translationSpeed;
  @FXML
  private EffectComponentGroup rotateSpeed;
  @FXML
  private EffectComponentGroup volume;
  @FXML
  private EffectComponentGroup backingMidi;

  public EffectsController() {
    this.wobbleEffect = new WobbleEffect(DEFAULT_SAMPLE_RATE);
    this.perspectiveEffect = new PerspectiveEffect(DEFAULT_SAMPLE_RATE);
    this.translateEffect = new TranslateEffect(DEFAULT_SAMPLE_RATE, 1, new Vector2());
    this.rotateEffect = new RotateEffect(DEFAULT_SAMPLE_RATE);
  }

  private <K, V> Map<K, V> mergeEffectMaps(Function<EffectComponentGroup, Map<K, V>> map) {
    return effects()
      .stream()
      .map(map)
      .flatMap(m -> m.entrySet().stream())
      .collect(Collectors.toMap(Map.Entry::getKey, Map.Entry::getValue));
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return mergeEffectMaps(effect -> effect.controller.getMidiButtonMap());
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return mergeEffectMaps(effect -> effect.controller.getComboBoxAnimatorMap());
  }

  // selects or deselects the given audio effect
  public void updateEffect(EffectType type, boolean checked, SettableEffect effect) {
    if (type != null) {
      if (checked) {
        audioPlayer.addEffect(type, effect);
      } else {
        audioPlayer.removeEffect(type);
      }
    }
  }

  public void setAudioDevice(AudioDevice device) {
    wobbleEffect.setSampleRate(device.sampleRate());
    perspectiveEffect.setSampleRate(device.sampleRate());
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
    KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> wobble.controller.getAnimator().setValue(0));
    KeyFrame kf2 = new KeyFrame(Duration.seconds(1), e -> {
      wobbleEffect.update();
      wobble.controller.updateAnimatorValue();
    });
    Timeline timeline = new Timeline(kf1, kf2);
    Platform.runLater(timeline::play);
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    wobble.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, wobbleEffect));
    perspective.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, perspectiveEffect));
    traceMin.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMin)));
    traceMax.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMax)));
    vectorCancelling.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new VectorCancellingEffect()));
    bitCrush.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new BitCrushEffect()));
    smoothing.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new SmoothEffect(1)));
    verticalDistort.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new VerticalDistortEffect(0.2)));
    horizontalDistort.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new HorizontalDistortEffect(0.2)));
    translationScale.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, translateEffect));
    translationSpeed.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(translateEffect::setSpeed)));
    rotateSpeed.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, rotateEffect));
    volume.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect((value) -> audioPlayer.setVolume(value / 3.0))));
    backingMidi.controller.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setBackingMidiVolume)));

    effects().forEach(effect -> {
      effect.controller.getAnimator().setMin(effect.getMin());
      effect.controller.getAnimator().setMax(effect.getMax());
      effect.controller.setEffectUpdater(this::updateEffect);

      // specific functionality for some effects
      switch (effect.getType()) {
        case WOBBLE -> effect.controller.onToggle((animator, value, selected) -> wobbleEffect.update());
        case TRACE_MIN, TRACE_MAX -> effect.controller.onToggle((animator, value, selected) -> {
          if (selected) {
            animator.setValue(value);
          } else if (effect.getType().equals(EffectType.TRACE_MAX)) {
            animator.setValue(1.0);
          } else {
            animator.setValue(0.0);
          }
        });
      }

      effect.controller.lateInitialize();

      if (effect.getAlwaysEnabled()) {
        effect.removeCheckBox();
      }
    });
  }

  private List<EffectComponentGroup> effects() {
    return List.of(
      vectorCancelling,
      bitCrush,
      verticalDistort,
      horizontalDistort,
      wobble,
      smoothing,
      traceMin,
      traceMax,
      perspective,
      translationScale,
      translationSpeed,
      rotateSpeed,
      volume,
      backingMidi
    );
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    return effects().stream().flatMap(effect -> effect.controller.micCheckBoxes().stream()).toList();
  }
  @Override
  public List<Slider> sliders() {
    return effects().stream().flatMap(effect -> effect.controller.sliders().stream()).toList();
  }
  @Override
  public List<String> labels() {
    return effects().stream().flatMap(effect -> effect.controller.labels().stream()).toList();
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("checkBoxes");
    effects().forEach(effect -> element.appendChild(effect.controller.save(document)));
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("checkBoxes").item(0);
    effects().forEach(effect -> effect.controller.load(element));
  }

  @Override
  public void slidersUpdated() {}

  public TranslateEffect getTranslateEffect() {
    return translateEffect;
  }
}
