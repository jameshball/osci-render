package sh.ball.gui.components;

import javafx.beans.InvalidationListener;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.AnimationType;
import sh.ball.audio.effect.EffectAnimator;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.effect.SettableEffect;
import sh.ball.gui.ThreeParamRunnable;
import sh.ball.gui.controller.SubController;

import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;

public class EffectComponentGroupController implements Initializable, SubController {

  private EffectAnimator animator;
  private EffectType type;
  private String label;
  private double increment;

  @FXML
  CheckBox effectCheckBox;
  @FXML
  Slider slider;
  @FXML
  Spinner<Double> spinner;
  @FXML
  SVGPath midi;
  @FXML
  ComboBox<AnimationType> comboBox;
  @FXML
  CheckBox micCheckBox;

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    slider.valueProperty().addListener((o, oldValue, newValue) ->
      spinner.setValueFactory(
        new SpinnerValueFactory.DoubleSpinnerValueFactory(
          slider.getMin(), slider.getMax(), newValue.doubleValue(), increment
        )
      ));
    spinner.valueProperty().addListener((o, oldValue, newValue) -> slider.setValue(newValue));
    spinner.setOnScroll((e) -> {
      double delta = e.getDeltaY() > 0 ? increment : -increment;
      spinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(
        slider.getMin(), slider.getMax(), slider.getValue() + delta, increment
      ));
    });
  }

  public void lateInitialize() {
    slider.setId(label);

    List<AnimationType> animations = List.of(AnimationType.STATIC, AnimationType.SEESAW, AnimationType.LINEAR, AnimationType.FORWARD, AnimationType.REVERSE);

    comboBox.setItems(FXCollections.observableList(animations));
    comboBox.setValue(AnimationType.STATIC);
    comboBox.valueProperty().addListener((options, old, animationType) ->
      animator.setAnimation(animationType));

    slider.minProperty().addListener((e, old, min) -> {
      animator.setMin(min.doubleValue());
      spinner.setValueFactory(
        new SpinnerValueFactory.DoubleSpinnerValueFactory(
          min.doubleValue(), slider.getMax(), slider.getValue(), increment
        )
      );
    });
    slider.maxProperty().addListener((e, old, max) -> {
      animator.setMax(max.doubleValue());
      spinner.setValueFactory(
        new SpinnerValueFactory.DoubleSpinnerValueFactory(
          slider.getMin(), max.doubleValue(), slider.getValue(), increment
        )
      );
    });
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(midi, slider);
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    return List.of(micCheckBox);
  }

  @Override
  public List<Slider> sliders() {
    return List.of(slider);
  }

  @Override
  public List<String> labels() {
    return List.of(label);
  }

  @Override
  public Element save(Document document) {
    Element checkBox = document.createElement(label);
    Element selected = document.createElement("selected");
    selected.appendChild(
      document.createTextNode(effectCheckBox.selectedProperty().getValue().toString())
    );
    Element animation = document.createElement("animation");
    animation.appendChild(document.createTextNode(comboBox.getValue().toString()));
    checkBox.appendChild(selected);
    checkBox.appendChild(animation);
    return checkBox;
  }

  @Override
  public void load(Element element) {
    Element checkBox = (Element) element.getElementsByTagName(label).item(0);
    // backwards compatibility
    if (checkBox == null) {
      effectCheckBox.setSelected(false);
      return;
    }
    String selected;
    // backwards compatibility
    if (checkBox.getElementsByTagName("selected").getLength() > 0) {
      selected = checkBox.getElementsByTagName("selected").item(0).getTextContent();
      if (checkBox.getElementsByTagName("animation").getLength() > 0) {
        String animation = checkBox.getElementsByTagName("animation").item(0).getTextContent();
        comboBox.setValue(AnimationType.fromString(animation));
      }
    } else {
      selected = checkBox.getTextContent();
      comboBox.setValue(AnimationType.STATIC);
    }
    effectCheckBox.setSelected(Boolean.parseBoolean(selected));
  }

  @Override
  public void slidersUpdated() {}

  public void setType(EffectType type) {
    this.type = type;
  }

  public EffectType getType() {
    return type;
  }

  public String getLabel() {
    return label;
  }

  public void setLabel(String label) {
    this.label = label;
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return Map.of(comboBox, animator);
  }

  public Map<EffectType, Slider> getEffectSliderMap() {
    return Map.of(type, slider);
  }

  public void setEffectUpdater(ThreeParamRunnable<EffectType, Boolean, SettableEffect> updater) {
    slider.valueProperty().addListener((e, old, value) -> animator.setValue(value.doubleValue()));
    effectCheckBox.selectedProperty().addListener(e -> {
      animator.setValue(slider.getValue());
      boolean selected = effectCheckBox.isSelected();
      updater.run(type, selected, animator);
      slider.setDisable(!selected);
      spinner.setDisable(!selected);
    });
  }

  public void onToggle(ThreeParamRunnable<EffectAnimator, Double, Boolean> onToggle) {
    effectCheckBox.selectedProperty().addListener((e, old, selected) ->
      onToggle.run(animator, slider.getValue(), selected));
  }

  public void setAnimator(EffectAnimator animator) {
    this.animator = animator;
  }

  public EffectAnimator getAnimator() {
    return animator;
  }

  public void updateAnimatorValue() {
    animator.setValue(slider.getValue());
  }

  public void setIncrement(double increment) {
    this.increment = increment;
  }

  public double getIncrement() {
    return increment;
  }

  public void setMajorTickUnit(double tickUnit) {
    slider.setMajorTickUnit(tickUnit);
  }

  public double getMajorTickUnit() {
    return slider.getMajorTickUnit();
  }
}
