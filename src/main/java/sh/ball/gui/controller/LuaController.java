package sh.ball.gui.controller;

import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.effect.*;
import sh.ball.gui.components.EffectComponentGroup;

import java.net.URL;
import java.util.*;

import static sh.ball.gui.Gui.audioPlayer;

public class LuaController implements Initializable, SubController {

  private MainController mainController;
  private boolean active = false;

  @FXML
  private EffectComponentGroup luaA;
  @FXML
  private EffectComponentGroup luaB;
  @FXML
  private EffectComponentGroup luaC;
  @FXML
  private EffectComponentGroup luaD;
  @FXML
  private EffectComponentGroup luaE;
  @FXML
  private Button resetStepCounterButton;

  public void setMainController(MainController mainController) {
    this.mainController = mainController;
  }

  @Override
  public List<EffectComponentGroup> effects() {
    return List.of(luaA, luaB, luaC, luaD, luaE);
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    Map<SVGPath, Slider> map = new HashMap<>();
    effects().forEach(effect -> map.putAll(effect.getMidiButtonMap()));
    return map;
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    effects().forEach(effect ->
      effect.setAnimator(new EffectAnimator(EffectAnimator.DEFAULT_SAMPLE_RATE, new ConsumerEffect(value -> updateLuaVariable(effect.getId(), value))))
    );

    effects().forEach(effect -> effect.setEffectUpdater(audioPlayer::updateEffect));

    resetStepCounterButton.setOnAction(e -> mainController.resetLuaStep());
  }

  private void updateLuaVariable(String id, double value) {
    if (mainController != null && active) {
      mainController.setLuaVariable("slider_" + id.toLowerCase().charAt(3), value);
    }
  }

  public void updateLuaVariables() {
    effects().forEach(effect -> updateLuaVariable(effect.getId(), effect.getValue()));
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return effects().stream().map(EffectComponentGroup::isMicSelectedProperty).toList();
  }

  @Override
  public List<Slider> sliders() {
    return effects().stream().map(EffectComponentGroup::sliders).flatMap(Collection::stream).toList();
  }

  @Override
  public List<String> labels() {
    return effects().stream().map(EffectComponentGroup::getLabel).toList();
  }

  @Override
  public List<Element> save(Document document) {
    return List.of(document.createElement("null"));
  }

  @Override
  public void load(Element root) {}

  @Override
  public void micNotAvailable() {
    effects().forEach(EffectComponentGroup::micNotAvailable);
  }

  public void setActive(boolean active) {
    this.active = active;
  }
}
