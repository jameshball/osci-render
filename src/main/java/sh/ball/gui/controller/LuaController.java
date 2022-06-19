package sh.ball.gui.controller;

import javafx.application.Platform;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;

import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;

public class LuaController implements Initializable, SubController {

  private MainController mainController;

  @FXML
  private Slider luaASlider;
  @FXML
  private SVGPath luaAMidi;
  @FXML
  private CheckBox luaAMic;
  @FXML
  private Slider luaBSlider;
  @FXML
  private SVGPath luaBMidi;
  @FXML
  private CheckBox luaBMic;
  @FXML
  private Slider luaCSlider;
  @FXML
  private SVGPath luaCMidi;
  @FXML
  private CheckBox luaCMic;
  @FXML
  private Slider luaDSlider;
  @FXML
  private SVGPath luaDMidi;
  @FXML
  private CheckBox luaDMic;
  @FXML
  private Slider luaESlider;
  @FXML
  private SVGPath luaEMidi;
  @FXML
  private CheckBox luaEMic;
  @FXML
  private Button resetStepCounterButton;

  public void setMainController(MainController mainController) {
    this.mainController = mainController;
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      luaAMidi, luaASlider,
      luaBMidi, luaBSlider,
      luaCMidi, luaCSlider,
      luaDMidi, luaDSlider,
      luaEMidi, luaESlider
    );
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    sliders().forEach(slider ->
      slider.valueProperty().addListener(e -> updateLuaVariable(slider))
    );

    resetStepCounterButton.setOnAction(e -> mainController.resetLuaStep());
  }

  private void updateLuaVariable(Slider slider) {
    mainController.setLuaVariable("slider_" + slider.getId().toLowerCase().charAt(3), slider.getValue());
  }

  public void updateLuaVariables() {
    sliders().forEach(this::updateLuaVariable);
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    return List.of(luaAMic, luaBMic, luaCMic, luaDMic, luaEMic);
  }

  @Override
  public List<Slider> sliders() {
    return List.of(luaASlider, luaBSlider, luaCSlider, luaDSlider, luaESlider);
  }

  @Override
  public List<String> labels() {
    return List.of("luaA", "luaB", "luaC", "luaD", "luaE");
  }

  @Override
  public List<Element> save(Document document) {
    return List.of(document.createElement("null"));
  }

  @Override
  public void load(Element root) {
  }

  @Override
  public void slidersUpdated() {}
}
