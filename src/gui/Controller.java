package gui;

import java.net.URL;
import java.util.ResourceBundle;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Slider;
import javafx.scene.text.Text;

public class Controller implements Initializable {

  @FXML
  private Text sliderOutput;
  @FXML
  private Slider slider;

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    slider.valueProperty().addListener(
        (source, oldValue, newValue) -> sliderOutput.setText(String.valueOf(slider.getValue())));
  }
}
