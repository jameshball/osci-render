package sh.ball.gui.components;

import javafx.beans.property.*;
import javafx.fxml.FXMLLoader;
import javafx.geometry.Insets;
import javafx.scene.control.*;
import javafx.scene.layout.HBox;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.AnimationType;
import sh.ball.audio.effect.EffectAnimator;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.effect.SettableEffect;
import sh.ball.gui.ThreeParamRunnable;

import java.io.IOException;
import java.util.List;
import java.util.Map;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class EffectComponentGroup extends HBox {

  private final EffectComponentGroupController controller;

  private final StringProperty label = new SimpleStringProperty("effect");
  private final StringProperty name = new SimpleStringProperty("Effect");
  private final Property<EffectType> type = new SimpleObjectProperty<>();
  private final BooleanProperty alwaysEnabled = new SimpleBooleanProperty(false);
  private final BooleanProperty spinnerHidden = new SimpleBooleanProperty(false);
  private final DoubleProperty increment = new SimpleDoubleProperty();
  private final DoubleProperty min = new SimpleDoubleProperty();
  private final DoubleProperty max = new SimpleDoubleProperty();
  private final BooleanProperty micSelected = new SimpleBooleanProperty(false);

  public EffectComponentGroup() {
    EffectComponentGroupController temp;
    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/effectComponentGroup.fxml"));
    loader.setRoot(this);
    loader.setClassLoader(getClass().getClassLoader());
    try {
      loader.load();
      temp = loader.getController();
      temp.setModel(this);
    } catch (IOException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
      temp = null;
    }
    controller = temp;
  }

  public String getName() {
    return name.get();
  }

  public void setName(String name) {
    this.name.set(name);
  }

  public StringProperty nameProperty() {
    return name;
  }

  public synchronized void hideSpinner() {
    controller.spinner.setVisible(false);
    controller.spinner.setPrefWidth(0);
    controller.label.setPrefWidth(90);
    controller.slider.setPrefWidth(170);
    HBox.setMargin(controller.midi, new Insets(11, 5, 0, -7));
  }

  public synchronized void removeCheckBox() {
    getChildren().set(1, controller.label);
  }

  public void setType(EffectType type) {
    this.type.setValue(type);
  }

  public EffectType getType() {
    return type.getValue();
  }

  public Property<EffectType> typeProperty() {
    return type;
  }

  public void setMin(double min) {
    this.min.setValue(min);
  }

  public double getMin() {
    return min.get();
  }

  public DoubleProperty minProperty() {
    return min;
  }

  public void setMax(double max) {
    this.max.setValue(max);
  }

  public double getMax() {
    return max.get();
  }

  public DoubleProperty maxProperty() {
    return max;
  }

  public double getValue() {
    return controller.slider.getValue();
  }

  public void setValue(double value) {
    controller.setValue(value);
  }

  public String getLabel() {
    return label.get();
  }

  public void setLabel(String label) {
    this.label.set(label);
  }

  public StringProperty labelProperty() {
    return label;
  }


  public void setIncrement(double increment) {
    this.increment.setValue(increment);
  }

  public double getIncrement() {
    return increment.get();
  }

  public DoubleProperty incrementProperty() {
    return increment;
  }

  public void setMajorTickUnit(double tickUnit) {
    controller.setMajorTickUnit(tickUnit);
  }

  public double getMajorTickUnit() {
    return controller.getMajorTickUnit();
  }

  public boolean isAlwaysEnabled() {
    return alwaysEnabled.get();
  }

  public void setAlwaysEnabled(boolean alwaysEnabled) {
    this.alwaysEnabled.set(alwaysEnabled);
  }

  public BooleanProperty isAlwaysEnabledProperty() {
    return alwaysEnabled;
  }

  public boolean isSpinnerHidden() {
    return spinnerHidden.get();
  }

  public void setSpinnerHidden(boolean spinnerHidden) {
    this.spinnerHidden.set(spinnerHidden);
  }

  public BooleanProperty isSpinnerHiddenProperty() {
    return spinnerHidden;
  }

  public void setAnimator(EffectAnimator animator) {
    controller.setAnimator(animator);
  }

  public EffectAnimator getAnimator() {
    return controller.getAnimator();
  }

  public void setEffectUpdater(ThreeParamRunnable<EffectType, Boolean, SettableEffect> updateEffect) {
    controller.setEffectUpdater(updateEffect);
  }

  public void onToggle(ThreeParamRunnable<EffectAnimator, Double, Boolean> onToggle) {
    controller.onToggle(onToggle);
  }

  public void setInactive(boolean inactive) {
    controller.setInactive(inactive);
  }

  public Map<SVGPath, Slider> getMidiButtonMap() {
    return controller.getMidiButtonMap();
  }

  public Map<ComboBox<AnimationType>, EffectAnimator> getComboBoxAnimatorMap() {
    return controller.getComboBoxAnimatorMap();
  }

  public void updateAnimatorValue() {
    controller.updateAnimatorValue();
  }

  public boolean isMicSelected() {
    return micSelected.get();
  }

  public void setMicSelected(boolean micSelected) {
    this.micSelected.set(micSelected);
  }

  public BooleanProperty isMicSelectedProperty() {
    return micSelected;
  }

  public List<BooleanProperty> micSelected() {
    return controller.micSelected();
  }

  public List<Slider> sliders() {
    return controller.sliders();
  }

  public List<String> labels() {
    return controller.labels();
  }

  public List<Element> save(Document document) {
    return controller.save(document);
  }

  public void load(Element element) {
    controller.load(element);
  }

  public void micNotAvailable() {
    controller.micNotAvailable();
  }

  public void setAnimationType(AnimationType type) {
    controller.comboBox.setValue(type);
  }
}
