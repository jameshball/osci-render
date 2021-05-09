package sh.ball.gui;

import javafx.scene.control.*;
import sh.ball.audio.AudioPlayer;
import sh.ball.audio.effect.Effect;
import sh.ball.audio.FrameProducer;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

import javafx.beans.InvalidationListener;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.effect.EventFactory;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.ObjFrameSettings;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class Controller implements Initializable {

  private static final InputStream DEFAULT_OBJ = Controller.class.getResourceAsStream("/models/cube.obj");

  private final FileChooser fileChooser = new FileChooser();
  // TODO: Reduce coupling on AudioPlayer
  private final AudioPlayer renderer = new AudioPlayer();
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  private FrameProducer<List<Shape>> producer = new FrameProducer<>(
    renderer,
    new ObjParser(DEFAULT_OBJ).parse()
  );

  private Stage stage;

  @FXML
  private Button chooseFileButton;
  @FXML
  private Label fileLabel;
  @FXML
  private TextField translationXTextField;
  @FXML
  private TextField translationYTextField;
  @FXML
  private Slider weightSlider;
  @FXML
  private Label weightLabel;
  @FXML
  private Slider rotateSpeedSlider;
  @FXML
  private Label rotateSpeedLabel;
  @FXML
  private Slider translationSpeedSlider;
  @FXML
  private Label translationSpeedLabel;
  @FXML
  private Slider scaleSlider;
  @FXML
  private Label scaleLabel;
  @FXML
  private TitledPane objTitledPane;
  @FXML
  private Slider focalLengthSlider;
  @FXML
  private Label focalLengthLabel;
  @FXML
  private TextField cameraXTextField;
  @FXML
  private TextField cameraYTextField;
  @FXML
  private TextField cameraZTextField;
  @FXML
  private CheckBox vectorCancellingCheckBox;
  @FXML
  private Slider vectorCancellingSlider;
  @FXML
  private CheckBox bitCrushCheckBox;
  @FXML
  private Slider bitCrushSlider;

  public Controller() throws IOException {
  }

  private Map<Slider, SliderUpdater<Double>> initializeSliderMap() {
    return Map.of(
      weightSlider,
      new SliderUpdater<>(weightLabel::setText, renderer::setQuality),
      rotateSpeedSlider,
      new SliderUpdater<>(rotateSpeedLabel::setText, renderer::setRotationSpeed),
      translationSpeedSlider,
      new SliderUpdater<>(translationSpeedLabel::setText, renderer::setTranslationSpeed),
      scaleSlider,
      new SliderUpdater<>(scaleLabel::setText, renderer::setScale),
      focalLengthSlider,
      new SliderUpdater<>(focalLengthLabel::setText, this::setFocalLength)
    );
  }

  private Map<EffectType, Slider> effectTypes;

  private void initializeEffectTypes() {
    effectTypes = Map.of(
      EffectType.VECTOR_CANCELLING,
      vectorCancellingSlider,
      EffectType.BIT_CRUSH,
      bitCrushSlider
    );
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    Map<Slider, SliderUpdater<Double>> sliders = initializeSliderMap();
    initializeEffectTypes();

    for (Slider slider : sliders.keySet()) {
      slider.valueProperty().addListener((source, oldValue, newValue) ->
        sliders.get(slider).update(slider.getValue())
      );
    }

    InvalidationListener translationUpdate = observable ->
      renderer.setTranslation(new Vector2(
        tryParse(translationXTextField.getText()),
        tryParse(translationYTextField.getText())
      ));

    translationXTextField.textProperty().addListener(translationUpdate);
    translationYTextField.textProperty().addListener(translationUpdate);

    InvalidationListener cameraPosUpdate = observable ->
      producer.setFrameSettings(new ObjFrameSettings(new Vector3(
        tryParse(cameraXTextField.getText()),
        tryParse(cameraYTextField.getText()),
        tryParse(cameraZTextField.getText())
    )));


    cameraXTextField.textProperty().addListener(cameraPosUpdate);
    cameraYTextField.textProperty().addListener(cameraPosUpdate);
    cameraZTextField.textProperty().addListener(cameraPosUpdate);

    InvalidationListener vectorCancellingListener = e ->
      updateEffect(EffectType.VECTOR_CANCELLING, vectorCancellingCheckBox.isSelected(),
        EventFactory.vectorCancelling((int) vectorCancellingSlider.getValue()));
    InvalidationListener bitCrushListener = e ->
      updateEffect(EffectType.BIT_CRUSH, bitCrushCheckBox.isSelected(),
        EventFactory.bitCrush(bitCrushSlider.getValue()));

    vectorCancellingSlider.valueProperty().addListener(vectorCancellingListener);
    vectorCancellingCheckBox.selectedProperty().addListener(vectorCancellingListener);

    bitCrushSlider.valueProperty().addListener(bitCrushListener);
    bitCrushCheckBox.selectedProperty().addListener(bitCrushListener);

    chooseFileButton.setOnAction(e -> {
      File file = fileChooser.showOpenDialog(stage);
      if (file != null) {
        chooseFile(file);
      }
    });

    executor.submit(producer);
    new Thread(renderer).start();
  }

  private void setFocalLength(double focalLength) {
    Vector3 pos = (Vector3) producer.setFrameSettings(new ObjFrameSettings(focalLength));
    cameraXTextField.setText(String.valueOf(pos.getX()));
    cameraYTextField.setText(String.valueOf(pos.getY()));
    cameraZTextField.setText(String.valueOf(pos.getZ()));
  }

  private double tryParse(String value) {
    try {
      return Double.parseDouble(value);
    } catch (NumberFormatException e) {
      return 0;
    }
  }

  private void updateEffect(EffectType type, boolean checked, Effect effect) {
    if (checked) {
      renderer.addEffect(type, effect);
      effectTypes.get(type).setDisable(false);
    } else {
      renderer.removeEffect(type);
      effectTypes.get(type).setDisable(true);
    }
  }

  private void chooseFile(File file) {
    try {
      producer.stop();
      String path = file.getAbsolutePath();
      producer = new FrameProducer<>(
        renderer,
        ParserFactory.getParser(path).parse()
      );
      executor.submit(producer);

      if (file.exists() && !file.isDirectory()) {
        fileLabel.setText(path);
        objTitledPane.setDisable(!ObjParser.isObjFile(path));
      } else {
        objTitledPane.setDisable(true);
      }
    } catch (IOException | ParserConfigurationException | SAXException ioException) {
      ioException.printStackTrace();
    }
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }
}
