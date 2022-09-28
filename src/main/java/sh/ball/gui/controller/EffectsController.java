package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.control.Button;
import javafx.scene.control.TextField;
import javafx.scene.paint.Color;
import javafx.scene.shape.SVGPath;
import javafx.util.Duration;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.FrequencyAnalyser;
import sh.ball.audio.effect.*;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.gui.Gui;
import sh.ball.gui.components.EffectComponentGroup;
import sh.ball.parser.lua.LuaExecutor;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.awt.*;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.List;
import java.util.function.Function;
import java.util.stream.Collectors;

import static sh.ball.audio.effect.EffectAnimator.DEFAULT_SAMPLE_RATE;
import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.math.Math.tryParse;

public class EffectsController implements Initializable, SubController {
  private static final String DEFAULT_SCRIPT = "return { x, y, z }";

  private final LuaExecutor executor = new LuaExecutor(LuaExecutor.STANDARD_GLOBALS);

  private final WobbleEffect wobbleEffect;
  private final PerspectiveEffect perspectiveEffect;
  private final TranslateEffect translateEffect;
  private final RotateEffect rotateEffect;

  private double scrollDelta = 0.05;
  private String script = DEFAULT_SCRIPT;

  private boolean setFixedAngleX = false;
  private boolean setFixedAngleY = false;
  private boolean setFixedAngleZ = false;

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
  private EffectComponentGroup depthScale;
  @FXML
  private EffectComponentGroup rotateSpeed3D;
  @FXML
  private EffectComponentGroup rotateX;
  @FXML
  private EffectComponentGroup rotateY;
  @FXML
  private EffectComponentGroup rotateZ;
  @FXML
  private EffectComponentGroup zPos;
  @FXML
  private EffectComponentGroup translationScale;
  @FXML
  private EffectComponentGroup translationSpeed;
  @FXML
  private EffectComponentGroup rotateSpeed;
  @FXML
  private EffectComponentGroup backingMidi;
  @FXML
  private TextField translationXTextField;
  @FXML
  private TextField translationYTextField;
  @FXML
  private CheckBox translateEllipseCheckBox;
  @FXML
  private CheckBox translateCheckBox;
  @FXML
  private Button resetTranslationButton;
  @FXML
  private Button resetRotationButton;
  @FXML
  private Button resetPerspectiveRotationButton;
  @FXML
  private Button depthFunctionButton;
  @FXML
  private SVGPath fixedAngleX;
  @FXML
  private SVGPath fixedAngleY;
  @FXML
  private SVGPath fixedAngleZ;

  public EffectsController() {
    this.wobbleEffect = new WobbleEffect(DEFAULT_SAMPLE_RATE);
    this.perspectiveEffect = new PerspectiveEffect(executor);
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
    return mergeEffectMaps(EffectComponentGroup::getMidiButtonMap);
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return mergeEffectMaps(EffectComponentGroup::getComboBoxAnimatorMap);
  }

  // selects or deselects the given audio effect
  public void updateEffect(EffectType type, boolean checked, SettableEffect effect) {
    audioPlayer.updateEffect(type, checked, effect);

    if (type == EffectType.DEPTH_3D) {
      Platform.runLater(() ->
        List.of(rotateSpeed3D, rotateX, rotateY, rotateZ, zPos).forEach(ecg -> ecg.setInactive(!checked))
      );
    }
  }

  public void setAudioDevice(AudioDevice device) {
    wobbleEffect.setSampleRate(device.sampleRate());
    translateEffect.setSampleRate(device.sampleRate());
    rotateEffect.setSampleRate(device.sampleRate());
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
    KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> wobble.getAnimator().setValue(0));
    KeyFrame kf2 = new KeyFrame(Duration.seconds(1), e -> {
      wobbleEffect.update();
      wobble.updateAnimatorValue();
    });
    Timeline timeline = new Timeline(kf1, kf2);
    Platform.runLater(timeline::play);
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    wobble.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, wobbleEffect));
    depthScale.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, perspectiveEffect));
    rotateSpeed3D.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(perspectiveEffect::setRotateSpeed)));
    rotateX.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect((this::setRotationX))));
    rotateY.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect((this::setRotationY))));
    rotateZ.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect((this::setRotationZ))));
    zPos.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect((perspectiveEffect::setZPos))));
    traceMin.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMin)));
    traceMax.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setTraceMax)));
    vectorCancelling.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new VectorCancellingEffect()));
    bitCrush.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new BitCrushEffect()));
    smoothing.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new SmoothEffect(1)));
    verticalDistort.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new VerticalDistortEffect(0.2)));
    horizontalDistort.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new HorizontalDistortEffect(0.2)));
    translationScale.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, translateEffect));
    translationSpeed.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(translateEffect::setSpeed)));
    rotateSpeed.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, rotateEffect));
    backingMidi.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(audioPlayer::setBackingMidiVolume)));

    effects().forEach(effect -> {
      effect.setEffectUpdater(this::updateEffect);

      // specific functionality for some effects
      switch (effect.getType()) {
        case WOBBLE -> effect.onToggle((animator, value, selected) -> wobbleEffect.update());
        case TRACE_MIN, TRACE_MAX -> effect.onToggle((animator, value, selected) -> {
          if (selected) {
            animator.setValue(value);
          } else if (effect.getType().equals(EffectType.TRACE_MAX)) {
            animator.setValue(1.0);
          } else {
            animator.setValue(0.0);
          }
        });
      }
    });

    resetTranslationButton.setOnAction(e -> {
      translationXTextField.setText("0.00");
      translationYTextField.setText("0.00");
    });

    translationXTextField.textProperty().addListener(e -> updateTranslation());
    translationYTextField.textProperty().addListener(e -> updateTranslation());

    translateEllipseCheckBox.selectedProperty().addListener((e, old, ellipse) -> translateEffect.setEllipse(ellipse));

    resetRotationButton.setOnAction(e -> {
      rotateSpeed.setValue(0);
      rotateEffect.resetTheta();
    });

    resetPerspectiveRotationButton.setOnAction(e -> {
      rotateX.setValue(0);
      rotateY.setValue(0);
      rotateZ.setValue(0);
      rotateSpeed3D.setValue(0);
      perspectiveEffect.resetRotation();
    });

    executor.setScript(script);
    depthFunctionButton.setOnAction(e -> Gui.launchCodeEditor(script, "3D Perspective Effect Depth Function", "text/x-lua", this::updateDepthFunction));

    List.of(rotateSpeed3D, rotateX, rotateY, rotateZ, zPos).forEach(ecg -> ecg.setInactive(true));

    fixedAngleX.setOnMouseClicked(e -> {
      setFixedAngleX = !setFixedAngleX;
      setFixedAngle(setFixedAngleX, fixedAngleX, rotateX);
    });
    fixedAngleY.setOnMouseClicked(e -> {
      setFixedAngleY = !setFixedAngleY;
      setFixedAngle(setFixedAngleY, fixedAngleY, rotateY);
    });
    fixedAngleZ.setOnMouseClicked(e -> {
      setFixedAngleZ = !setFixedAngleZ;
      setFixedAngle(setFixedAngleZ, fixedAngleZ, rotateZ);
    });
  }

  private void setFixedAngle(boolean fixedAngle, SVGPath fixedAngleButton, EffectComponentGroup rotate) {
    fixedAngleButton.setFill(fixedAngle ? Color.RED : Color.WHITE);
    rotate.getAnimator().updateValue();
  }

  private void updateDepthFunction(byte[] fileData, String fileName) {
    script = new String(fileData, StandardCharsets.UTF_8);
    executor.setScript(script);
  }

  @Override
  public List<EffectComponentGroup> effects() {
    return List.of(
      vectorCancelling,
      bitCrush,
      verticalDistort,
      horizontalDistort,
      wobble,
      smoothing,
      traceMin,
      traceMax,
      rotateSpeed3D,
      zPos,
      rotateX,
      rotateY,
      rotateZ,
      depthScale,
      translationScale,
      translationSpeed,
      rotateSpeed,
      backingMidi
    );
  }

  private void setRotationX(double x) {
    if (setFixedAngleX) {
      perspectiveEffect.setActualRotationX(x);
    } else {
      perspectiveEffect.setRotationX(x);
    }
  }

  private void setRotationY(double y) {
    if (setFixedAngleY) {
      perspectiveEffect.setActualRotationY(y);
    } else {
      perspectiveEffect.setRotationY(y);
    }
  }

  private void setRotationZ(double z) {
    if (setFixedAngleZ) {
      perspectiveEffect.setActualRotationZ(z);
    } else {
      perspectiveEffect.setRotationZ(z);
    }
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return effects().stream().flatMap(effect -> effect.micSelected().stream()).toList();
  }
  @Override
  public List<Slider> sliders() {
    return effects().stream().flatMap(effect -> effect.sliders().stream()).toList();
  }
  @Override
  public List<String> labels() {
    return effects().stream().flatMap(effect -> effect.labels().stream()).toList();
  }

  @Override
  public List<Element> save(Document document) {
    Element translation = document.createElement("translation");
    Element x = document.createElement("x");
    x.appendChild(document.createTextNode(translationXTextField.getText()));
    Element y = document.createElement("y");
    y.appendChild(document.createTextNode(translationYTextField.getText()));
    Element ellipse = document.createElement("ellipse");
    ellipse.appendChild(document.createTextNode(Boolean.toString(translateEllipseCheckBox.isSelected())));
    translation.appendChild(x);
    translation.appendChild(y);
    translation.appendChild(ellipse);

    Element depthFunction = document.createElement("depthFunction");
    String encodedData = Base64.getEncoder().encodeToString(script.getBytes(StandardCharsets.UTF_8));
    depthFunction.appendChild(document.createTextNode(encodedData));

    Element perspectiveFixedRotate = document.createElement("perspectiveFixedRotate");
    Element fixedRotateX = document.createElement("x");
    fixedRotateX.appendChild(document.createTextNode(String.valueOf(setFixedAngleX)));
    Element fixedRotateY = document.createElement("y");
    fixedRotateY.appendChild(document.createTextNode(String.valueOf(setFixedAngleY)));
    Element fixedRotateZ = document.createElement("z");
    fixedRotateZ.appendChild(document.createTextNode(String.valueOf(setFixedAngleZ)));
    perspectiveFixedRotate.appendChild(fixedRotateX);
    perspectiveFixedRotate.appendChild(fixedRotateY);
    perspectiveFixedRotate.appendChild(fixedRotateZ);

    return List.of(translation, depthFunction, perspectiveFixedRotate);
  }

  @Override
  public void load(Element root) {
    Element translation = (Element) root.getElementsByTagName("translation").item(0);
    Element x = (Element) translation.getElementsByTagName("x").item(0);
    Element y = (Element) translation.getElementsByTagName("y").item(0);
    translationXTextField.setText(x.getTextContent());
    translationYTextField.setText(y.getTextContent());

    // For backwards compatibility we assume a default value
    Element ellipse = (Element) translation.getElementsByTagName("ellipse").item(0);
    translateEllipseCheckBox.setSelected(ellipse != null && Boolean.parseBoolean(ellipse.getTextContent()));

    Element depthFunction = (Element) root.getElementsByTagName("depthFunction").item(0);
    // backwards compatibility
    if (depthFunction == null) {
      script = DEFAULT_SCRIPT;
    } else {
      String encodedScript = depthFunction.getTextContent();
      script = new String(Base64.getDecoder().decode(encodedScript), StandardCharsets.UTF_8);
      executor.setScript(script);
    }

    Element perspectiveFixedRotate = (Element) root.getElementsByTagName("perspectiveFixedRotate").item(0);
    if (perspectiveFixedRotate == null) {
      setFixedAngleX = false;
      setFixedAngleY = false;
      setFixedAngleZ = false;
    } else {
      Element fixedRotateX = (Element) perspectiveFixedRotate.getElementsByTagName("x").item(0);
      setFixedAngleX = fixedRotateX != null && Boolean.parseBoolean(fixedRotateX.getTextContent());

      Element fixedRotateY = (Element) perspectiveFixedRotate.getElementsByTagName("y").item(0);
      setFixedAngleY = fixedRotateY != null && Boolean.parseBoolean(fixedRotateY.getTextContent());

      Element fixedRotateZ = (Element) perspectiveFixedRotate.getElementsByTagName("z").item(0);
      setFixedAngleZ = fixedRotateZ != null && Boolean.parseBoolean(fixedRotateZ.getTextContent());
    }

    setFixedAngle(setFixedAngleX, fixedAngleX, rotateX);
    setFixedAngle(setFixedAngleY, fixedAngleY, rotateY);
    setFixedAngle(setFixedAngleZ, fixedAngleZ, rotateZ);
  }

  @Override
  public void micNotAvailable() {
    effects().forEach(EffectComponentGroup::micNotAvailable);
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

  public boolean mouseTranslate() {
    return translateCheckBox.isSelected();
  }

  public void disableMouseTranslate() {
    translateCheckBox.setSelected(false);
  }
}
