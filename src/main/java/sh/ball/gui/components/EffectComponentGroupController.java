package sh.ball.gui.components;

import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.value.ChangeListener;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.paint.Color;
import javafx.scene.shape.SVGPath;
import javafx.scene.text.TextAlignment;
import javafx.util.StringConverter;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.AnimationType;
import sh.ball.audio.effect.EffectAnimator;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.effect.SettableEffect;
import sh.ball.gui.ThreeParamRunnable;
import sh.ball.gui.controller.SubController;

import java.net.URL;
import java.text.DecimalFormat;
import java.text.ParseException;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class EffectComponentGroupController implements Initializable, SubController {

  private EffectAnimator animator;
  private ThreeParamRunnable<EffectType, Boolean, SettableEffect> effectUpdater;
  private EffectComponentGroup model;

  private final StringConverter<Double> doubleConverter = new StringConverter<>() {
    private final DecimalFormat df = new DecimalFormat("###.###");

    @Override
    public String toString(Double object) {
      if (object == null) {return "";}
      return df.format(object);
    }

    @Override
    public Double fromString(String string) {
      try {
        if (string == null) {
          return null;
        }
        string = string.trim();
        if (string.length() < 1) {
          return null;
        }
        return df.parse(string).doubleValue();
      } catch (ParseException ex) {
        logger.log(Level.WARNING, ex.getMessage(), ex);
        return null;
      }
    }
  };

  public Label label = new Label();

  @FXML
  public CheckBox effectCheckBox;
  @FXML
  public Slider slider;
  @FXML
  public Spinner<Double> spinner;
  @FXML
  public SVGPath midi;
  @FXML
  public ComboBox<AnimationType> comboBox;
  @FXML
  public SVGPath mic;

  public void setModel(EffectComponentGroup model) {
    this.model = model;
    model.labelProperty().addListener((e, old, label) -> slider.setId(label));

    model.nameProperty().addListener((e, old, name) -> {
      effectCheckBox.setText(name);
      if (model.isAlwaysEnabled()) {
        model.removeCheckBox();
      }
      setInactive(model.isAlwaysEnabled());
      label.setText(name);
    });

    model.isAlwaysEnabledProperty().addListener((e, old, alwaysEnabled) -> {
      effectCheckBox.setSelected(true);
      if (alwaysEnabled) {
        model.removeCheckBox();
      }
      setInactive(alwaysEnabled);
    });

    ChangeListener<Number> listener = (e, old, value) -> {
      slider.setMin(model.getMin());
      slider.setMax(model.getMax());
      slider.setBlockIncrement(model.getIncrement());
      updateSpinnerValueFactory(model.getMin(), model.getMax(), slider.getValue(), model.getIncrement());
    };

    model.minProperty().addListener(listener);
    model.maxProperty().addListener(listener);
    model.incrementProperty().addListener(listener);

    mic.setOnMouseClicked(e -> model.isMicSelectedProperty().setValue(!model.isMicSelected()));

    model.isMicSelectedProperty().addListener((e, old, selected) -> {
      if (selected) {
        mic.setFill(Color.RED);
      } else {
        mic.setFill(Color.WHITE);
      }
    });

    model.isSpinnerHiddenProperty().addListener((e, old, spinnerHidden) -> {
      if (spinnerHidden) {
        model.hideSpinner();
      }
    });
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    updateSpinnerValueFactory(0, 1, 0, 0.005);

    spinner.valueProperty().addListener((o, oldValue, newValue) -> slider.setValue(newValue));
    slider.valueProperty().addListener((o, oldValue, newValue) -> spinner.getValueFactory().setValue(newValue.doubleValue()));

    List<AnimationType> animations = List.of(AnimationType.STATIC, AnimationType.SEESAW, AnimationType.LINEAR, AnimationType.FORWARD, AnimationType.REVERSE);

    comboBox.setItems(FXCollections.observableList(animations));
    comboBox.setValue(AnimationType.STATIC);

    label.setTextAlignment(TextAlignment.LEFT);
    label.setPrefWidth(effectCheckBox.getPrefWidth());
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(midi, slider);
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return List.of(model.isMicSelectedProperty());
  }

  @Override
  public List<Slider> sliders() {
    return List.of(slider);
  }

  @Override
  public List<String> labels() {
    return List.of(model.getLabel());
  }

  @Override
  public List<Element> save(Document document) {
    Element checkBox = document.createElement(model.getLabel());
    Element selected = document.createElement("selected");
    selected.appendChild(
      document.createTextNode(effectCheckBox.selectedProperty().getValue().toString())
    );
    Element animation = document.createElement("animation");
    animation.appendChild(document.createTextNode(comboBox.getValue().toString()));
    checkBox.appendChild(selected);
    checkBox.appendChild(animation);
    return List.of(checkBox);
  }

  @Override
  public void load(Element element) {
    Element checkBox = (Element) element.getElementsByTagName(model.getLabel()).item(0);
    // backwards compatibility
    if (checkBox == null) {
      if (!model.isAlwaysEnabled()) {
        effectCheckBox.setSelected(false);
      }
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
    effectCheckBox.setSelected(Boolean.parseBoolean(selected) || model.isAlwaysEnabled());
  }

  @Override
  public void micNotAvailable() {
    mic.setDisable(true);
    mic.setContent("M19 11c0 1.19-.34 2.3-.9 3.28l-1.23-1.23c.27-.62.43-1.31.43-2.05H19m-4 .16L9 5.18V5a3 3 0 0 1 3-3a3 3 0 0 1 3 3v6.16M4.27 3L21 19.73L19.73 21l-4.19-4.19c-.77.46-1.63.77-2.54.91V21h-2v-3.28c-3.28-.49-6-3.31-6-6.72h1.7c0 3 2.54 5.1 5.3 5.1c.81 0 1.6-.19 2.31-.52l-1.66-1.66L12 14a3 3 0 0 1-3-3v-.72L3 4.27L4.27 3Z");
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return Map.of(comboBox, animator);
  }

  public void setEffectUpdater(ThreeParamRunnable<EffectType, Boolean, SettableEffect> updater) {
    ChangeListener<Boolean> listener = (e, old, selected) -> {
      this.animator.setValue(slider.getValue());
      updater.run(model.getType(), selected, this.animator);
      slider.setDisable(!selected);
      spinner.setDisable(!selected);
    };

    effectCheckBox.selectedProperty().addListener(listener);

    // Re-trigger this event if we missed it
    if (effectCheckBox.isSelected()) {
      listener.changed(new SimpleBooleanProperty(true), false, true);
    }
  }

  public void onToggle(ThreeParamRunnable<EffectAnimator, Double, Boolean> onToggle) {
    effectCheckBox.selectedProperty().addListener((e, old, selected) ->
      onToggle.run(animator, slider.getValue(), selected));
  }

  public void setAnimator(EffectAnimator animator) {
    this.animator = animator;
    animator.setMin(model.getMin());
    animator.setMax(model.getMax());
    slider.valueProperty().addListener((e, old, value) -> this.animator.setValue(value.doubleValue()));
    comboBox.valueProperty().addListener((options, old, animationType) -> this.animator.setAnimation(animationType));
  }

  public EffectAnimator getAnimator() {
    return animator;
  }

  public void updateAnimatorValue() {
    animator.setValue(slider.getValue());
  }

  public void setMajorTickUnit(double tickUnit) {
    slider.setMajorTickUnit(tickUnit);
  }

  public double getMajorTickUnit() {
    return slider.getMajorTickUnit();
  }

  public void updateSpinnerValueFactory(double min, double max, double value, double increment) {
    SpinnerValueFactory<Double> svf = new SpinnerValueFactory.DoubleSpinnerValueFactory(min, max, value, increment);
    svf.setConverter(doubleConverter);
    spinner.setValueFactory(svf);
  }

  public void setInactive(boolean inactive) {
    slider.setDisable(!inactive);
    spinner.setDisable(!inactive);
  }

  public void setValue(double value) {
    slider.setValue(value);
    spinner.getValueFactory().setValue(value);
  }
}
