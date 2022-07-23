package sh.ball.gui.components;

import javafx.fxml.FXMLLoader;
import javafx.scene.control.*;
import javafx.scene.layout.HBox;
import javafx.scene.text.TextAlignment;
import sh.ball.audio.effect.EffectType;

import java.io.IOException;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class EffectComponentGroup extends HBox {

  public final EffectComponentGroupController controller;

  public EffectComponentGroup() {
    EffectComponentGroupController temp;
    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/effectComponentGroup.fxml"));
    loader.setRoot(this);
    loader.setClassLoader(getClass().getClassLoader());
    try {
      loader.load();
      temp = loader.getController();
    } catch (IOException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
      temp = null;
    }
    controller = temp;
  }

  public void setName(String name) {
    controller.effectCheckBox.setText(name);
  }

  public String getName() {
    return controller.effectCheckBox.getText();
  }

  public void removeCheckBox() {
    controller.effectCheckBox.setSelected(true);
    Label label = new Label(getName());
    label.setTextAlignment(TextAlignment.LEFT);
    label.setPrefWidth(controller.effectCheckBox.getPrefWidth());
    getChildren().set(1, label);
  }

  public void setType(EffectType type) {
    controller.setType(type);
  }

  public EffectType getType() {
    return controller.getType();
  }

  public void setMin(double min) {
    controller.slider.setMin(min);
    controller.updateSpinnerValueFactory(min, controller.slider.getMax(), controller.slider.getValue(), controller.getIncrement());
  }

  public double getMin() {
    return controller.slider.getMin();
  }

  public void setMax(double max) {
    controller.slider.setMax(max);
    controller.updateSpinnerValueFactory(controller.slider.getMin(), max, controller.slider.getValue(), controller.getIncrement());
  }

  public double getMax() {
    return controller.slider.getMax();
  }

  public double getValue() {
    return controller.slider.getValue();
  }

  public void setValue(double value) {
    controller.slider.setValue(value);
    controller.spinner.getValueFactory().setValue(value);
  }

  public void setLabel(String label) {
    controller.setLabel(label);
  }

  public String getLabel() {
    return controller.getLabel();
  }

  public void setIncrement(double increment) {
    controller.setIncrement(increment);
  }

  public double getIncrement() {
    return controller.getIncrement();
  }

  public void setMajorTickUnit(double tickUnit) {
    controller.setMajorTickUnit(tickUnit);
  }

  public double getMajorTickUnit() {
    return controller.getMajorTickUnit();
  }

  public void setAlwaysEnabled(boolean alwaysEnabled) {
    controller.setAlwaysEnabled(alwaysEnabled);
  }

  public boolean getAlwaysEnabled() {
    return controller.getAlwaysEnabled();
  }
}
