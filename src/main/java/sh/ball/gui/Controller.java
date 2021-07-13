package sh.ball.gui;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.scene.control.*;
import javafx.stage.DirectoryChooser;
import javafx.util.Duration;
import sh.ball.audio.*;
import sh.ball.audio.effect.*;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.*;
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
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiListener;
import sh.ball.audio.midi.MidiNote;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.Listener;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

public class Controller implements Initializable, FrequencyListener, MidiListener, Listener {

  private static final InputStream DEFAULT_OBJ = Controller.class.getResourceAsStream("/models/cube.obj");

  private final FileChooser fileChooser = new FileChooser();
  private final DirectoryChooser folderChooser = new DirectoryChooser();
  private final AudioPlayer<List<Shape>> audioPlayer;
  private final ExecutorService executor = Executors.newSingleThreadExecutor();

  private final RotateEffect rotateEffect;
  private final TranslateEffect translateEffect;
  private final WobbleEffect wobbleEffect;
  private final ScaleEffect scaleEffect;

  private int sampleRate;
  private FrequencyAnalyser<List<Shape>> analyser;
  private final AudioDevice defaultDevice;
  private boolean recording = false;
  private Timeline recordingTimeline;

  private FrameProducer<List<Shape>> producer;
  private final List<FrameSet<List<Shape>>> frameSets = new ArrayList<>();
  private final List<String> frameSetPaths = new ArrayList<>();
  private int currentFrameSet;

  private Stage stage;

  @FXML
  private Label frequencyLabel;
  @FXML
  private Button chooseFileButton;
  @FXML
  private Button chooseFolderButton;
  @FXML
  private Label fileLabel;
  @FXML
  private Label jkLabel;
  @FXML
  private Button recordButton;
  @FXML
  private Label recordLabel;
  @FXML
  private TextField recordTextField;
  @FXML
  private CheckBox recordCheckBox;
  @FXML
  private Label recordLengthLabel;
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
    frameSets.add(frames);
    frameSetPaths.add("cube.obj");
    currentFrameSet = 0;
    frames.addListener(this);
    this.producer = new FrameProducer<>(audioPlayer, frames);
    this.defaultDevice = audioPlayer.getDefaultDevice();
    if (defaultDevice == null) {
      throw new RuntimeException("No default audio device found!");
    }
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
        updateLastVisitedDirectory(new File(file.getParent()));
      }
    });

    chooseFolderButton.setOnAction(e -> {
      File file = folderChooser.showDialog(stage);
      if (file != null) {
        chooseFile(file);
        updateLastVisitedDirectory(file);
      }
    });

    recordButton.setOnAction(event -> toggleRecord());

    recordCheckBox.selectedProperty().addListener((e, oldVal, newVal) -> {
      recordLengthLabel.setDisable(!newVal);
      recordTextField.setDisable(!newVal);
    });

    updateObjectRotateSpeed();

    audioPlayer.addEffect(EffectType.SCALE, scaleEffect);
    audioPlayer.addEffect(EffectType.ROTATE, rotateEffect);
    audioPlayer.addEffect(EffectType.TRANSLATE, translateEffect);

    audioPlayer.setDevice(defaultDevice);
    deviceComboBox.setItems(FXCollections.observableList(audioPlayer.devices()));
    deviceComboBox.setValue(defaultDevice);

    executor.submit(producer);
    analyser = new FrequencyAnalyser<>(audioPlayer, 2, sampleRate);
    startFrequencyAnalyser(analyser);
    startAudioPlayerThread();
    MidiCommunicator midiCommunicator = new MidiCommunicator();
    midiCommunicator.addListener(this);
    new Thread(midiCommunicator).start();

    deviceComboBox.valueProperty().addListener((options, oldDevice, newDevice) -> {
      if (newDevice != null) {
        switchAudioDevice(newDevice);
      }
    });
  }

  private void updateLastVisitedDirectory(File file) {
    String lastVisitedDirectory = file != null ? file.getAbsolutePath() : System.getProperty("user.home");
    File dir = new File(lastVisitedDirectory);
    fileChooser.setInitialDirectory(dir);
    folderChooser.setInitialDirectory(dir);
  }

  private void switchAudioDevice(AudioDevice device) {
    try {
      audioPlayer.reset();
    } catch (Exception e) {
      e.printStackTrace();
    }
    audioPlayer.setDevice(device);
    analyser.stop();
    sampleRate = device.sampleRate();
    analyser = new FrequencyAnalyser<>(audioPlayer, 2, sampleRate);
    startFrequencyAnalyser(analyser);
    startAudioPlayerThread();
  }

  private void startAudioPlayerThread() {
    Thread audioPlayerThread = new Thread(audioPlayer);
    audioPlayerThread.setUncaughtExceptionHandler((thread, throwable) -> throwable.printStackTrace());
    audioPlayerThread.start();
  }

  private void startFrequencyAnalyser(FrequencyAnalyser<List<Shape>> analyser) {
    analyser.addListener(this);
    analyser.addListener(wobbleEffect);
    new Thread(analyser).start();
  }

  private void toggleRecord() {
    recording = !recording;
    boolean timedRecord = recordCheckBox.isSelected();
    if (recording) {
      if (timedRecord) {
        double recordingLength;
        try {
          recordingLength = Double.parseDouble(recordTextField.getText());
        } catch (NumberFormatException e) {
          recordLabel.setText("Please set a valid record length");
          recording = false;
          return;
        }
        recordButton.setText("Cancel");
        KeyFrame kf1 = new KeyFrame(
          Duration.seconds(0),
          e -> audioPlayer.startRecord()
        );
        KeyFrame kf2 = new KeyFrame(
          Duration.seconds(recordingLength),
          e -> {
            saveRecording();
            recording = false;
          }
        );
        recordingTimeline = new Timeline(kf1, kf2);
        Platform.runLater(recordingTimeline::play);
      } else {
        recordButton.setText("Stop Recording");
        audioPlayer.startRecord();
      }
      recordLabel.setText("Recording...");
    } else if (timedRecord) {
      recordingTimeline.stop();
      recordLabel.setText("");
      recordButton.setText("Record");
      audioPlayer.stopRecord();
    } else {
      saveRecording();
    }
  }

  private void saveRecording() {
    try {
      recordButton.setText("Record");
      AudioInputStream input = audioPlayer.stopRecord();
      File file = fileChooser.showSaveDialog(stage);
      SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss");
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

  private void changeFrameSet() {
    FrameSet<List<Shape>> frames = frameSets.get(currentFrameSet);
    producer.stop();
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
    fileLabel.setText(frameSetPaths.get(currentFrameSet));
    objTitledPane.setDisable(!ObjParser.isObjFile(frameSetPaths.get(currentFrameSet)));
  }

  private void chooseFile(File chosenFile) {
    try {
      if (chosenFile.exists()) {
        frameSets.clear();
        frameSetPaths.clear();

        if (chosenFile.isDirectory()) {
          jkLabel.setVisible(true);
          for (File file : chosenFile.listFiles()) {
            try {
              frameSets.add(ParserFactory.getParser(file.getAbsolutePath()).parse());
              frameSetPaths.add(file.getName());
            } catch (IOException ignored) {
            }
          }
        } else {
          jkLabel.setVisible(false);
          frameSets.add(ParserFactory.getParser(chosenFile.getAbsolutePath()).parse());
          frameSetPaths.add(chosenFile.getName());
        }

        currentFrameSet = 0;
        changeFrameSet();
      }
    } catch (IOException | ParserConfigurationException | SAXException ioException) {
      ioException.printStackTrace();

      // display error to user (for debugging purposes)
      String oldPath = fileLabel.getText();
      KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> fileLabel.setText(ioException.getMessage()));
      KeyFrame kf2 = new KeyFrame(Duration.seconds(5), e -> fileLabel.setText(oldPath));
      Timeline timeline = new Timeline(kf1, kf2);
      Platform.runLater(timeline::play);
    }
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }

  public void nextFrameSet() {
    currentFrameSet++;
    if (currentFrameSet >= frameSets.size()) {
      currentFrameSet = 0;
    }
    changeFrameSet();
  }

  public void previousFrameSet() {
    currentFrameSet--;
    if (currentFrameSet < 0) {
      currentFrameSet = frameSets.size() - 1;
    }
    changeFrameSet();
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

  @Override
  public void sendMidiMessage(int status, MidiNote note, int pressure) {
    if (note.key() == 1) {
      double max = weightSlider.getMax();
      double min = weightSlider.getMin();
      double range = max - min;
      weightSlider.setValue(min + (pressure / 127.0) * range);
    }
    System.out.println(status);
    System.out.println(note);
    System.out.println(pressure);
  }
}
