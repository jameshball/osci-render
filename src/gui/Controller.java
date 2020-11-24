package gui;

import audio.AudioPlayer;
import audio.FrameProducer;
import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import javafx.beans.InvalidationListener;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.control.Slider;
import javafx.scene.control.TextField;
import javafx.stage.FileChooser;
import javafx.stage.Stage;
import javax.xml.parsers.ParserConfigurationException;
import org.xml.sax.SAXException;
import shapes.Shape;
import shapes.Vector2;

public class Controller implements Initializable {

  private static final int BUFFER_SIZE = 20;
  private static final int SAMPLE_RATE = 192000;

  private final FileChooser fileChooser = new FileChooser();
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final AudioPlayer player = new AudioPlayer(SAMPLE_RATE, frameQueue);
  private final FrameProducer producer = new FrameProducer(frameQueue);

  private Stage stage;

  @FXML
  private Button chooseFileButton;
  @FXML
  public Label fileLabel;
  @FXML
  public TextField translationXTextField;
  @FXML
  public TextField translationYTextField;
  @FXML
  public Slider weightSlider;
  @FXML
  public Label weightLabel;
  @FXML
  public Slider rotateSpeedSlider;
  @FXML
  public Label rotateSpeedLabel;
  @FXML
  public Slider translationSpeedSlider;
  @FXML
  public Label translationSpeedLabel;
  @FXML
  public Slider scaleSlider;
  @FXML
  public Label scaleLabel;
  @FXML
  private Slider focalLengthSlider;
  @FXML
  private Label focalLengthLabel;

  public Controller() throws IOException, ParserConfigurationException, SAXException {
  }

  private Map<Slider, SliderUpdater<Double>> initializeSliderMap() {
    return Map.of(
        weightSlider,
        new SliderUpdater<>(weightLabel::setText, player::setWeight),
        rotateSpeedSlider,
        new SliderUpdater<>(rotateSpeedLabel::setText, player::setRotateSpeed),
        translationSpeedSlider,
        new SliderUpdater<>(translationSpeedLabel::setText, player::setTranslationSpeed),
        scaleSlider,
        new SliderUpdater<>(scaleLabel::setText, player::setScale),
        focalLengthSlider,
        new SliderUpdater<>(focalLengthLabel::setText, producer::setFocalLength)
    );
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    Map<Slider, SliderUpdater<Double>> sliders = initializeSliderMap();

    for (Slider slider : sliders.keySet()) {
      slider.valueProperty().addListener((source, oldValue, newValue) ->
          sliders.get(slider).update(slider.getValue())
      );
    }

    InvalidationListener translationUpdate = observable ->
        player.setTranslation(new Vector2(
            tryParse(translationXTextField.getText()),
            tryParse(translationYTextField.getText())
        ));

    translationXTextField.textProperty().addListener(translationUpdate);
    translationYTextField.textProperty().addListener(translationUpdate);

    chooseFileButton.setOnAction(e -> {
      File file = null;
      while (file == null) {
        file = fileChooser.showOpenDialog(stage);
      }
      try {
        producer.setParser(file.getAbsolutePath());
        fileLabel.setText(file.getAbsolutePath());
      } catch (IOException | ParserConfigurationException | SAXException ioException) {
        ioException.printStackTrace();
      }
    });

    new Thread(producer).start();
    new Thread(player).start();
  }

  private double tryParse(String value) {
    try {
      return Double.parseDouble(value);
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }
}
