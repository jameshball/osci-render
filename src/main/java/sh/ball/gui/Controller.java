package sh.ball.gui;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.scene.control.*;
import javafx.util.Duration;
import sh.ball.audio.*;
import sh.ball.audio.effect.*;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.ResourceBundle;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Consumer;

import javafx.beans.InvalidationListener;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.audio.effect.Effect;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.Listener;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class Controller implements Initializable, FrequencyListener, Listener {


  private static final InputStream DEFAULT_OBJ = Controller.class.getResourceAsStream("/models/cube.obj");

  private final FileChooser fileChooser = new FileChooser();
  private final AudioPlayer<List<Shape>> audioPlayer;
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  private final int sampleRate;

  private final RotateEffect rotateEffect;
  private final TranslateEffect translateEffect;
  private final WobbleEffect wobbleEffect;
  private final ScaleEffect scaleEffect;

  private AudioDevice defaultDevice;
  private FrameProducer<List<Shape>> producer;
  private boolean recording = false;

  private Stage stage;

  @FXML
  private Label frequencyLabel;
  @FXML
  private Button chooseFileButton;
  @FXML
  private Label fileLabel;
  @FXML
  private Button recordButton;
  @FXML
  private Label recordLabel;
  @FXML
  private TextField translationXTextField;
  @FXML
  private TextField translationYTextField;
  @FXML
  private Slider weightSlider;
  @FXML
  private Slider rotateSpeedSlider;
  @FXML
  private Slider translationSpeedSlider;
  @FXML
  private Slider scaleSlider;
  @FXML
  private TitledPane objTitledPane;
  @FXML
  private Slider focalLengthSlider;
  @FXML
  private TextField cameraXTextField;
  @FXML
  private TextField cameraYTextField;
  @FXML
  private TextField cameraZTextField;
  @FXML
  private Slider objectRotateSpeedSlider;
  @FXML
  private CheckBox rotateCheckBox;
  @FXML
  private CheckBox vectorCancellingCheckBox;
  @FXML
  private Slider vectorCancellingSlider;
  @FXML
  private CheckBox bitCrushCheckBox;
  @FXML
  private Slider bitCrushSlider;
  @FXML
  private CheckBox verticalDistortCheckBox;
  @FXML
  private Slider verticalDistortSlider;
  @FXML
  private CheckBox horizontalDistortCheckBox;
  @FXML
  private Slider horizontalDistortSlider;
  @FXML
  private CheckBox wobbleCheckBox;
  @FXML
  private Slider wobbleSlider;
  @FXML
  private ComboBox<AudioDevice> deviceComboBox;

  public Controller(AudioPlayer<List<Shape>> audioPlayer) throws IOException {
    this.audioPlayer = audioPlayer;
    FrameSet<List<Shape>> frames = new ObjParser(DEFAULT_OBJ).parse();
    frames.addListener(this);
    this.producer = new FrameProducer<>(audioPlayer, frames);
    this.defaultDevice = audioPlayer.getDefaultDevice();
    this.sampleRate = defaultDevice.sampleRate();
    this.rotateEffect = new RotateEffect(sampleRate);
    this.translateEffect = new TranslateEffect(sampleRate);
    this.wobbleEffect = new WobbleEffect(sampleRate);
    this.scaleEffect = new ScaleEffect();
  }

  private Map<Slider, Consumer<Double>> initializeSliderMap() {
    return Map.of(
      weightSlider,
      audioPlayer::setQuality,
      rotateSpeedSlider,
      rotateEffect::setSpeed,
      translationSpeedSlider,
      translateEffect::setSpeed,
      scaleSlider,
      scaleEffect::setScale,
      focalLengthSlider,
      d -> updateFocalLength(),
      objectRotateSpeedSlider,
      d -> updateObjectRotateSpeed()
    );
  }

  private Map<EffectType, Slider> effectTypes;

  private void initializeEffectTypes() {
    effectTypes = Map.of(
      EffectType.VECTOR_CANCELLING,
      vectorCancellingSlider,
      EffectType.BIT_CRUSH,
      bitCrushSlider,
      EffectType.VERTICAL_DISTORT,
      verticalDistortSlider,
      EffectType.HORIZONTAL_DISTORT,
      horizontalDistortSlider,
      EffectType.WOBBLE,
      wobbleSlider
    );
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    Map<Slider, Consumer<Double>> sliders = initializeSliderMap();
    initializeEffectTypes();

    for (Slider slider : sliders.keySet()) {
      slider.valueProperty().addListener((source, oldValue, newValue) ->
        sliders.get(slider).accept(slider.getValue())
      );
    }

    translationXTextField.textProperty().addListener(e -> updateTranslation());
    translationYTextField.textProperty().addListener(e -> updateTranslation());

    cameraXTextField.focusedProperty().addListener(e -> updateCameraPos());
    cameraYTextField.focusedProperty().addListener(e -> updateCameraPos());
    cameraZTextField.focusedProperty().addListener(e -> updateCameraPos());

    InvalidationListener vectorCancellingListener = e ->
      updateEffect(EffectType.VECTOR_CANCELLING, vectorCancellingCheckBox.isSelected(),
        EffectFactory.vectorCancelling((int) vectorCancellingSlider.getValue()));
    InvalidationListener bitCrushListener = e ->
      updateEffect(EffectType.BIT_CRUSH, bitCrushCheckBox.isSelected(),
        EffectFactory.bitCrush(bitCrushSlider.getValue()));
    InvalidationListener verticalDistortListener = e ->
      updateEffect(EffectType.VERTICAL_DISTORT, verticalDistortCheckBox.isSelected(),
        EffectFactory.verticalDistort(verticalDistortSlider.getValue()));
    InvalidationListener horizontalDistortListener = e ->
      updateEffect(EffectType.HORIZONTAL_DISTORT, horizontalDistortCheckBox.isSelected(),
        EffectFactory.horizontalDistort(horizontalDistortSlider.getValue()));
    InvalidationListener wobbleListener = e -> {
      wobbleEffect.setVolume(wobbleSlider.getValue());
      updateEffect(EffectType.WOBBLE, wobbleCheckBox.isSelected(), wobbleEffect);
    };

    vectorCancellingSlider.valueProperty().addListener(vectorCancellingListener);
    vectorCancellingCheckBox.selectedProperty().addListener(vectorCancellingListener);

    bitCrushSlider.valueProperty().addListener(bitCrushListener);
    bitCrushCheckBox.selectedProperty().addListener(bitCrushListener);

    verticalDistortSlider.valueProperty().addListener(verticalDistortListener);
    verticalDistortCheckBox.selectedProperty().addListener(verticalDistortListener);

    horizontalDistortSlider.valueProperty().addListener(horizontalDistortListener);
    horizontalDistortCheckBox.selectedProperty().addListener(horizontalDistortListener);

    wobbleSlider.valueProperty().addListener(wobbleListener);
    wobbleCheckBox.selectedProperty().addListener(wobbleListener);
    wobbleCheckBox.selectedProperty().addListener(e -> wobbleEffect.update());

    fileChooser.setInitialFileName("out.wav");
    fileChooser.getExtensionFilters().addAll(
      new FileChooser.ExtensionFilter("All Files", "*.*"),
      new FileChooser.ExtensionFilter("WAV Files", "*.wav"),
      new FileChooser.ExtensionFilter("Wavefront OBJ Files", "*.obj"),
      new FileChooser.ExtensionFilter("SVG Files", "*.svg"),
      new FileChooser.ExtensionFilter("Text Files", "*.txt")
    );

    chooseFileButton.setOnAction(e -> {
      File file = fileChooser.showOpenDialog(stage);
      if (file != null) {
        chooseFile(file);
      }
    });

    recordButton.setOnAction(event -> toggleRecord());

    updateObjectRotateSpeed();

    audioPlayer.addEffect(EffectType.SCALE, scaleEffect);
    audioPlayer.addEffect(EffectType.ROTATE, rotateEffect);
    audioPlayer.addEffect(EffectType.TRANSLATE, translateEffect);

    executor.submit(producer);
    audioPlayer.setDevice(defaultDevice);
    System.out.println(audioPlayer.devices());
    deviceComboBox.setItems(FXCollections.observableList(audioPlayer.devices()));
    deviceComboBox.getSelectionModel().select(defaultDevice);
    Thread renderThread = new Thread(audioPlayer);
    renderThread.setUncaughtExceptionHandler((thread, throwable) -> throwable.printStackTrace());
    renderThread.start();
    FrequencyAnalyser<List<Shape>> analyser = new FrequencyAnalyser<>(audioPlayer, 2, sampleRate);
    analyser.addListener(this);
    analyser.addListener(wobbleEffect);
    new Thread(analyser).start();
  }

  private void toggleRecord() {
    recording = !recording;
    if (recording) {
      recordLabel.setText("Recording...");
      recordButton.setText("Stop Recording");
      audioPlayer.startRecord();
    } else {
      recordButton.setText("Record");
      AudioInputStream input = audioPlayer.stopRecord();
      try {
        File file = fileChooser.showSaveDialog(stage);
        SimpleDateFormat formatter= new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss");
        Date date = new Date(System.currentTimeMillis());
        if (file == null) {
          file = new File("out-" + formatter.format(date) + ".wav");
        }
        AudioSystem.write(input, AudioFileFormat.Type.WAVE, file);
        input.close();
        recordLabel.setText("Saved to " + file.getAbsolutePath());
      } catch (IOException e) {
        recordLabel.setText("Error saving file");
        e.printStackTrace();
      }
    }
  }

  private void updateFocalLength() {
    double focalLength = focalLengthSlider.getValue();
    producer.setFrameSettings(ObjSettingsFactory.focalLength(focalLength));
  }

  private void updateObjectRotateSpeed() {
    double rotateSpeed = objectRotateSpeedSlider.getValue();
    producer.setFrameSettings(
      ObjSettingsFactory.rotateSpeed((Math.exp(3 * rotateSpeed) - 1) / 50)
    );
  }

  private void updateTranslation() {
    translateEffect.setTranslation(new Vector2(
      tryParse(translationXTextField.getText()),
      tryParse(translationYTextField.getText())
    ));
  }

  private void updateCameraPos() {
    producer.setFrameSettings(ObjSettingsFactory.cameraPosition(new Vector3(
      tryParse(cameraXTextField.getText()),
      tryParse(cameraYTextField.getText()),
      tryParse(cameraZTextField.getText())
    )));
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
      audioPlayer.addEffect(type, effect);
      effectTypes.get(type).setDisable(false);
    } else {
      audioPlayer.removeEffect(type);
      effectTypes.get(type).setDisable(true);
    }
  }

  private void chooseFile(File file) {
    try {
      producer.stop();
      String path = file.getAbsolutePath();
      FrameSet<List<Shape>> frames = ParserFactory.getParser(path).parse();
      frames.addListener(this);
      producer = new FrameProducer<>(audioPlayer, frames);

      updateObjectRotateSpeed();
      updateFocalLength();
      executor.submit(producer);

      KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> wobbleEffect.setVolume(0));
      KeyFrame kf2 = new KeyFrame(Duration.seconds(1), e -> {
        wobbleEffect.update();
        wobbleEffect.setVolume(wobbleSlider.getValue());
      });
      Timeline timeline = new Timeline(kf1, kf2);
      Platform.runLater(timeline::play);

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

  protected boolean mouseRotate() {
    return rotateCheckBox.isSelected();
  }

  protected void disableMouseRotate() {
    rotateCheckBox.setSelected(false);
  }

  protected void setObjRotate(Vector3 vector) {
    producer.setFrameSettings(ObjSettingsFactory.rotation(vector));
  }

  @Override
  public void updateFrequency(double leftFrequency, double rightFrequency) {
    Platform.runLater(() ->
      frequencyLabel.setText(String.format("L/R Frequency:\n%d Hz / %d Hz", Math.round(leftFrequency), Math.round(rightFrequency)))
    );
  }

  @Override
  public void update(Object pos) {
    if (pos instanceof Vector3 vector) {
      Platform.runLater(() -> {
        cameraXTextField.setText(String.valueOf(round(vector.getX(), 3)));
        cameraYTextField.setText(String.valueOf(round(vector.getY(), 3)));
        cameraZTextField.setText(String.valueOf(round(vector.getZ(), 3)));
      });
    }
  }

  private static double round(double value, double places) {
    if (places < 0) throw new IllegalArgumentException();

    long factor = (long) Math.pow(10, places);
    value = value * factor;
    long tmp = Math.round(value);
    return (double) tmp / factor;
  }
}
