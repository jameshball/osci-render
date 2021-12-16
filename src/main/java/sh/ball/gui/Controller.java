package sh.ball.gui;

import javafx.animation.*;
import javafx.application.Platform;
import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.collections.FXCollections;
import javafx.scene.control.*;
import javafx.scene.paint.Color;
import javafx.scene.paint.Paint;
import javafx.scene.shape.SVGPath;
import javafx.stage.DirectoryChooser;
import javafx.util.Duration;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;
import sh.ball.audio.*;
import sh.ball.audio.effect.*;

import java.io.*;
import java.net.URL;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.*;
import java.util.concurrent.*;
import java.util.function.Consumer;

import javafx.beans.InvalidationListener;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import javax.sound.midi.ShortMessage;
import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.xml.XMLConstants;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.transform.Transformer;
import javax.xml.transform.TransformerException;
import javax.xml.transform.TransformerFactory;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;

import org.xml.sax.SAXException;
import sh.ball.audio.effect.Effect;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.ConglomerateAudioEngine;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiListener;
import sh.ball.audio.midi.MidiNote;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import static sh.ball.math.Math.tryParse;

public class Controller implements Initializable, FrequencyListener, MidiListener {

  private String openProjectPath;

  // audio
  private static final double MAX_FREQUENCY = 12000;
  private final ShapeAudioPlayer audioPlayer;
  private final RotateEffect rotateEffect;
  private final TranslateEffect translateEffect;
  private final WobbleEffect wobbleEffect;
  private final SmoothEffect smoothEffect;
  private final DoubleProperty frequency;
  private final AudioDevice defaultDevice;
  private int sampleRate;
  private FrequencyAnalyser<List<Shape>> analyser;
  private boolean recording = false;
  private Timeline recordingTimeline;

  // midi
  private final Map<Integer, SVGPath> CCMap = new HashMap<>();
  private Paint armedMidiPaint;
  private SVGPath armedMidi;
  private Map<SVGPath, Slider> midiButtonMap;

  // frames
  private static final InputStream DEFAULT_OBJ = Controller.class.getResourceAsStream("/models/cube.obj");
  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  private final List<byte[]> openFiles = new ArrayList<>();
  private final List<String> frameSourcePaths = new ArrayList<>();
  private List<FrameSource<List<Shape>>> frameSources = new ArrayList<>();
  private FrameProducer<List<Shape>> producer;
  private int currentFrameSource;
  private Vector3 rotation = new Vector3();

  // javafx
  private final FileChooser osciFileChooser = new FileChooser();
  private final FileChooser wavFileChooser = new FileChooser();
  private final FileChooser renderFileChooser = new FileChooser();
  private final DirectoryChooser folderChooser = new DirectoryChooser();
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
  private Slider frequencySlider;
  @FXML
  private SVGPath frequencyMidi;
  @FXML
  private Slider rotateSpeedSlider;
  @FXML
  private SVGPath rotateSpeedMidi;
  @FXML
  private Slider translationSpeedSlider;
  @FXML
  private SVGPath translationSpeedMidi;
  @FXML
  private Slider volumeSlider;
  @FXML
  private SVGPath volumeMidi;
  @FXML
  private TitledPane objTitledPane;
  @FXML
  private Slider focalLengthSlider;
  @FXML
  private SVGPath focalLengthMidi;
  @FXML
  private Slider objectRotateSpeedSlider;
  @FXML
  private SVGPath objectRotateSpeedMidi;
  @FXML
  private CheckBox rotateCheckBox;
  @FXML
  private CheckBox vectorCancellingCheckBox;
  @FXML
  private Slider vectorCancellingSlider;
  @FXML
  private SVGPath vectorCancellingMidi;
  @FXML
  private CheckBox bitCrushCheckBox;
  @FXML
  private Slider bitCrushSlider;
  @FXML
  private SVGPath bitCrushMidi;
  @FXML
  private CheckBox verticalDistortCheckBox;
  @FXML
  private Slider verticalDistortSlider;
  @FXML
  private SVGPath verticalDistortMidi;
  @FXML
  private CheckBox horizontalDistortCheckBox;
  @FXML
  private Slider horizontalDistortSlider;
  @FXML
  private SVGPath horizontalDistortMidi;
  @FXML
  private CheckBox wobbleCheckBox;
  @FXML
  private Slider wobbleSlider;
  @FXML
  private SVGPath wobbleMidi;
  @FXML
  private CheckBox smoothCheckBox;
  @FXML
  private Slider smoothSlider;
  @FXML
  private SVGPath smoothMidi;
  @FXML
  private Slider octaveSlider;
  @FXML
  private SVGPath octaveMidi;
  @FXML
  private CheckBox traceCheckBox;
  @FXML
  private Slider traceSlider;
  @FXML
  private SVGPath traceMidi;
  @FXML
  private Slider visibilitySlider;
  @FXML
  private SVGPath visibilityMidi;
  @FXML
  private ComboBox<AudioDevice> deviceComboBox;
  @FXML
  private MenuItem openProjectMenuItem;
  @FXML
  private MenuItem saveProjectMenuItem;
  @FXML
  private MenuItem saveAsProjectMenuItem;

  public Controller() throws Exception {
    MidiCommunicator midiCommunicator = new MidiCommunicator();
    midiCommunicator.addListener(this);
    new Thread(midiCommunicator).start();

    this.audioPlayer = new ShapeAudioPlayer(ConglomerateAudioEngine::new, midiCommunicator);

    // Clone DEFAULT_OBJ InputStream using a ByteArrayOutputStream
    ByteArrayOutputStream baos = new ByteArrayOutputStream();
    assert DEFAULT_OBJ != null;
    DEFAULT_OBJ.transferTo(baos);
    InputStream objClone = new ByteArrayInputStream(baos.toByteArray());
    FrameSource<List<Shape>> frames = new ObjParser(objClone).parse();

    openFiles.add(baos.toByteArray());
    frameSources.add(frames);
    frameSourcePaths.add("cube.obj");
    currentFrameSource = 0;
    this.producer = new FrameProducer<>(audioPlayer, frames);
    this.defaultDevice = audioPlayer.getDefaultDevice();
    if (defaultDevice == null) {
      throw new RuntimeException("No default audio device found!");
    }
    this.sampleRate = defaultDevice.sampleRate();
    this.rotateEffect = new RotateEffect(sampleRate);
    this.translateEffect = new TranslateEffect(sampleRate);
    this.wobbleEffect = new WobbleEffect(sampleRate);
    this.smoothEffect = new SmoothEffect(1);
    this.frequency = new SimpleDoubleProperty(0);
  }

  // initialises midiButtonMap by mapping MIDI logo SVGs to the slider that they
  // control if they are selected.
  private Map<SVGPath, Slider> initializeMidiButtonMap() {
    Map<SVGPath, Slider> midiMap = new HashMap<>();
    midiMap.put(frequencyMidi, frequencySlider);
    midiMap.put(rotateSpeedMidi, rotateSpeedSlider);
    midiMap.put(translationSpeedMidi, translationSpeedSlider);
    midiMap.put(volumeMidi, volumeSlider);
    midiMap.put(focalLengthMidi, focalLengthSlider);
    midiMap.put(objectRotateSpeedMidi, objectRotateSpeedSlider);
    midiMap.put(vectorCancellingMidi, vectorCancellingSlider);
    midiMap.put(bitCrushMidi, bitCrushSlider);
    midiMap.put(wobbleMidi, wobbleSlider);
    midiMap.put(smoothMidi, smoothSlider);
    midiMap.put(octaveMidi, octaveSlider);
    midiMap.put(traceMidi, traceSlider);
    midiMap.put(visibilityMidi, visibilitySlider);
    midiMap.put(verticalDistortMidi, verticalDistortSlider);
    midiMap.put(horizontalDistortMidi, horizontalDistortSlider);
    return midiMap;
  }

  // Maps sliders to the functions that they should call whenever their value
  // changes.
  private Map<Slider, Consumer<Double>> initializeSliderMap() {
    return Map.of(
      rotateSpeedSlider, rotateEffect::setSpeed,
      translationSpeedSlider, translateEffect::setSpeed,
      focalLengthSlider, this::updateFocalLength,
      objectRotateSpeedSlider, this::updateObjectRotateSpeed,
      visibilitySlider, audioPlayer::setMainFrequencyScale
    );
  }

  private Map<EffectType, Slider> effectTypes;

  // Maps EffectTypes to the slider that controls the effect so that they can be
  // toggled when the appropriate checkbox is ticked.
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
      wobbleSlider,
      EffectType.SMOOTH,
      smoothSlider
    );
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    // converts the value of frequencySlider to the actual frequency that it represents so that it
    // can increase at an exponential scale.
    frequencySlider.valueProperty().addListener((o, old, f) -> frequency.set(Math.pow(MAX_FREQUENCY, f.doubleValue())));
    frequency.addListener((o, old, f) -> frequencySlider.setValue(Math.log(f.doubleValue()) / Math.log(MAX_FREQUENCY)));
    audioPlayer.setFrequency(frequency);
    // default value is middle C
    frequency.set(MidiNote.MIDDLE_C);
    audioPlayer.setVolume(volumeSlider.valueProperty());

    this.midiButtonMap = initializeMidiButtonMap();

    midiButtonMap.keySet().forEach(midi -> midi.setOnMouseClicked(e -> {
      if (armedMidi == midi) {
        // we are already armed, so we should unarm
        midi.setFill(armedMidiPaint);
        armedMidiPaint = null;
        armedMidi = null;
      } else {
        // not yet armed
        if (armedMidi != null) {
          armedMidi.setFill(armedMidiPaint);
        }
        armedMidiPaint = midi.getFill();
        armedMidi = midi;
        midi.setFill(Color.RED);
      }
    }));

    Map<Slider, Consumer<Double>> sliders = initializeSliderMap();
    initializeEffectTypes();

    for (Slider slider : sliders.keySet()) {
      slider.valueProperty().addListener((source, oldValue, newValue) ->
        sliders.get(slider).accept(newValue.doubleValue())
      );
    }

    translationXTextField.textProperty().addListener(e -> updateTranslation());
    translationYTextField.textProperty().addListener(e -> updateTranslation());

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
    InvalidationListener smoothListener = e -> {
      smoothEffect.setWindowSize((int) smoothSlider.getValue());
      updateEffect(EffectType.SMOOTH, smoothCheckBox.isSelected(), smoothEffect);
    };
    InvalidationListener traceListener = e -> {
      double trace = traceCheckBox.isSelected() ? traceSlider.valueProperty().getValue() : 1;
      audioPlayer.setTrace(trace);
      traceSlider.setDisable(!traceCheckBox.isSelected());
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

    smoothSlider.valueProperty().addListener(smoothListener);
    smoothCheckBox.selectedProperty().addListener(smoothListener);

    octaveSlider.valueProperty().addListener((e, old, octave) -> audioPlayer.setOctave(octave.intValue()));

    traceSlider.valueProperty().addListener(traceListener);
    traceCheckBox.selectedProperty().addListener(traceListener);

    osciFileChooser.setInitialFileName("project.osci");
    osciFileChooser.getExtensionFilters().add(
      new FileChooser.ExtensionFilter("osci-render files", "*.osci")
    );
    wavFileChooser.setInitialFileName("out.wav");
    wavFileChooser.getExtensionFilters().addAll(
      new FileChooser.ExtensionFilter("WAV Files", "*.wav"),
      new FileChooser.ExtensionFilter("All Files", "*.*")
    );
    // when opening new files, we support .obj, .svg, and .txt
    renderFileChooser.getExtensionFilters().addAll(
      new FileChooser.ExtensionFilter("All Files", "*.*"),
      new FileChooser.ExtensionFilter("Wavefront OBJ Files", "*.obj"),
      new FileChooser.ExtensionFilter("SVG Files", "*.svg"),
      new FileChooser.ExtensionFilter("Text Files", "*.txt")
    );

    saveProjectMenuItem.setOnAction(e -> {
      if (openProjectPath != null) {
        saveProject(openProjectPath);
      } else {
        File file = osciFileChooser.showSaveDialog(stage);
        if (file != null) {
          updateLastVisitedDirectory(new File(file.getParent()));
          saveProject(file.getAbsolutePath());
        }
      }
    });

    saveAsProjectMenuItem.setOnAction(e -> {
      File file = osciFileChooser.showSaveDialog(stage);
      if (file != null) {
        updateLastVisitedDirectory(new File(file.getParent()));
        saveProject(file.getAbsolutePath());
      }
    });

    openProjectMenuItem.setOnAction(e -> {
      File file = osciFileChooser.showOpenDialog(stage);
      if (file != null) {
        updateLastVisitedDirectory(new File(file.getParent()));
        openProject(file.getAbsolutePath());
      }
    });

    chooseFileButton.setOnAction(e -> {
      File file = renderFileChooser.showOpenDialog(stage);
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

    updateObjectRotateSpeed(rotateSpeedSlider.getValue());

    audioPlayer.addEffect(EffectType.ROTATE, rotateEffect);
    audioPlayer.addEffect(EffectType.TRANSLATE, translateEffect);

    audioPlayer.setDevice(defaultDevice);
    List<AudioDevice> devices = audioPlayer.devices();
    deviceComboBox.setItems(FXCollections.observableList(devices));
    deviceComboBox.setValue(defaultDevice);

    executor.submit(producer);
    analyser = new FrequencyAnalyser<>(audioPlayer, 2, sampleRate);
    startFrequencyAnalyser(analyser);
    startAudioPlayerThread();

    deviceComboBox.valueProperty().addListener((options, oldDevice, newDevice) -> {
      if (newDevice != null) {
        switchAudioDevice(newDevice);
      }
    });
  }

  // used when a file is chosen so that the same folder is reopened when a
  // file chooser opens
  private void updateLastVisitedDirectory(File file) {
    String lastVisitedDirectory = file != null ? file.getAbsolutePath() : System.getProperty("user.home");
    File dir = new File(lastVisitedDirectory);
    osciFileChooser.setInitialDirectory(dir);
    wavFileChooser.setInitialDirectory(dir);
    renderFileChooser.setInitialDirectory(dir);
    folderChooser.setInitialDirectory(dir);
  }

  // restarts audioPlayer and FrequencyAnalyser to support new device
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

  // creates a new thread for the audioPlayer and starts it
  private void startAudioPlayerThread() {
    Thread audioPlayerThread = new Thread(audioPlayer);
    audioPlayerThread.setUncaughtExceptionHandler((thread, throwable) -> throwable.printStackTrace());
    audioPlayerThread.start();
  }

  // creates a new thread for the frequency analyser and adds the wobble effect and the controller
  // as listeners of it so that they can get updates as the frequency changes
  private void startFrequencyAnalyser(FrequencyAnalyser<List<Shape>> analyser) {
    analyser.addListener(this);
    analyser.addListener(wobbleEffect);
    new Thread(analyser).start();
  }

  // alternates between recording and not recording when called.
  // If it is a non-timed recording, it is saved when this is called and
  // recording is stopped. If it is a time recording, this function will cancel
  // the recording.
  private void toggleRecord() {
    recording = !recording;
    boolean timedRecord = recordCheckBox.isSelected();
    if (recording) {
      // if it is a timed recording then a timeline is scheduled to start and
      // stop recording at the predefined times.
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
        // save the recording after recordingLength seconds
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
      // cancel the recording
      recordingTimeline.stop();
      recordLabel.setText("");
      recordButton.setText("Record");
      audioPlayer.stopRecord();
    } else {
      saveRecording();
    }
  }

  // Stops recording and opens a fileChooser so that the user can choose a
  // location to save the recording to. If no location is chosen then it will
  // be saved as the current date-time of the machine.
  private void saveRecording() {
    try {
      recordButton.setText("Record");
      AudioInputStream input = audioPlayer.stopRecord();
      File file = wavFileChooser.showSaveDialog(stage);
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

  // changes the focalLength of the FrameProducer
  private void updateFocalLength(double focalLength) {
    producer.setFrameSettings(ObjSettingsFactory.focalLength(focalLength));
  }

  // changes the rotateSpeed of the FrameProducer
  private void updateObjectRotateSpeed(double rotateSpeed) {
    producer.setFrameSettings(
      ObjSettingsFactory.rotateSpeed((Math.exp(3 * rotateSpeed) - 1) / 50)
    );
  }

  // changes the sinusoidal translation of the image rendered
  private void updateTranslation() {
    translateEffect.setTranslation(new Vector2(
      tryParse(translationXTextField.getText()),
      tryParse(translationYTextField.getText())
    ));
  }

  // selects or deselects the given audio effect
  private void updateEffect(EffectType type, boolean checked, Effect effect) {
    if (checked) {
      audioPlayer.addEffect(type, effect);
      if (effectTypes.containsKey(type)) {
        effectTypes.get(type).setDisable(false);
      }
    } else {
      audioPlayer.removeEffect(type);
      if (effectTypes.containsKey(type)) {
        effectTypes.get(type).setDisable(true);
      }
    }
  }

  // changes the FrameProducer e.g. could be changing from a 3D object to an
  // SVG. The old FrameProducer is stopped and a new one created and initialised
  // with the same settings that the original had.
  private void changeFrameSource(int index) {
    index = Math.max(0, Math.min(index, frameSources.size() - 1));
    currentFrameSource = index;
    FrameSource<List<Shape>> frames = frameSources.get(index);
    frameSources.forEach(FrameSource::disable);
    frames.enable();
    producer = new FrameProducer<>(audioPlayer, frames);

    // Apply the same settings that the previous frameSource had
    updateObjectRotateSpeed(objectRotateSpeedSlider.getValue());
    updateFocalLength(focalLengthSlider.getValue());
    setObjRotate(rotation);
    executor.submit(producer);

    // apply the wobble effect after a second as the frequency of the audio takes a while to
    // propagate and send to its listeners.
    KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> wobbleEffect.setVolume(0));
    KeyFrame kf2 = new KeyFrame(Duration.seconds(1), e -> {
      wobbleEffect.update();
      wobbleEffect.setVolume(wobbleSlider.getValue());
    });
    Timeline timeline = new Timeline(kf1, kf2);
    Platform.runLater(timeline::play);

    fileLabel.setText(frameSourcePaths.get(index));
    // enable the .obj file settings iff the new frameSource is for a 3D object.
    objTitledPane.setDisable(!ObjParser.isObjFile(frameSourcePaths.get(index)));
  }

  private void updateFiles(List<byte[]> files, List<String> names) throws IOException, ParserConfigurationException, SAXException {
    List<FrameSource<List<Shape>>> oldFrameSources = frameSources;
    frameSources = new ArrayList<>();
    frameSourcePaths.clear();
    openFiles.clear();
    jkLabel.setVisible(files.size() > 1);

    for (int i = 0; i < files.size(); i++) {
      try {
        frameSources.add(ParserFactory.getParser(names.get(i), files.get(i)).parse());
        frameSourcePaths.add(names.get(i));
        openFiles.add(files.get(i));
      } catch (IOException ignored) {}
    }

    oldFrameSources.forEach(FrameSource::disable);
    changeFrameSource(0);
  }

  // selects a new file or folder for files to be rendered from
  private void chooseFile(File chosenFile) {
    try {
      if (chosenFile.exists()) {
        List<byte[]> files = new ArrayList<>();
        List<String> names = new ArrayList<>();
        if (chosenFile.isDirectory()) {
          for (File file : Objects.requireNonNull(chosenFile.listFiles())) {
            files.add(Files.readAllBytes(file.toPath()));
            names.add(file.getName());
          }
        } else {
          files.add(Files.readAllBytes(chosenFile.toPath()));
          names.add(chosenFile.getName());
        }

        updateFiles(files, names);
      }
    } catch (IOException | ParserConfigurationException | SAXException ioException) {
      ioException.printStackTrace();

      // display error to user (for debugging purposes)
      String oldPath = fileLabel.getText();
      // shows the error message and later shows the old path as the file being rendered
      // doesn't change
      KeyFrame kf1 = new KeyFrame(Duration.seconds(0), e -> fileLabel.setText(ioException.getMessage()));
      KeyFrame kf2 = new KeyFrame(Duration.seconds(5), e -> fileLabel.setText(oldPath));
      Timeline timeline = new Timeline(kf1, kf2);
      Platform.runLater(timeline::play);
    }
  }

  // used so that the Controller has access to the stage, allowing it to open
  // file directories etc.
  public void setStage(Stage stage) {
    this.stage = stage;
  }

  // increments and changes the frameSource after pressing 'j'
  public void nextFrameSet() {
    if (frameSources.size() == 1) {
      return;
    }
    int index = currentFrameSource + 1;
    if (index >= frameSources.size()) {
      index = 0;
    }
    changeFrameSource(index);
  }

  // decrements and changes the frameSource after pressing 'k'
  public void previousFrameSource() {
    if (frameSources.size() == 1) {
      return;
    }
    int index = currentFrameSource - 1;
    if (index < 0) {
      index = frameSources.size() - 1;
    }
    changeFrameSource(index);
  }

  // determines whether the mouse is being used to rotate a 3D object
  protected boolean mouseRotate() {
    return rotateCheckBox.isSelected();
  }

  // stops the mouse rotating the 3D object when ESC is pressed or checkbox is
  // unchecked
  protected void disableMouseRotate() {
    rotateCheckBox.setSelected(false);
  }

  // updates the 3D object rotation angle
  protected void setObjRotate(Vector3 vector) {
    rotation = vector;
    producer.setFrameSettings(ObjSettingsFactory.rotation(vector));
  }

  @Override
  public void updateFrequency(double leftFrequency, double rightFrequency) {
    Platform.runLater(() ->
      frequencyLabel.setText(String.format("L/R Frequency:\n%d Hz / %d Hz", Math.round(leftFrequency), Math.round(rightFrequency)))
    );
  }

  private void mapMidiCC(int cc, SVGPath midiButton) {
    if (CCMap.containsValue(midiButton)) {
      CCMap.values().remove(midiButton);
    }
    if (CCMap.containsKey(cc)) {
      CCMap.get(cc).setFill(Color.WHITE);
    }
    CCMap.put(cc, midiButton);
    midiButton.setFill(Color.LIME);
  }

  // converts MIDI pressure value into a valid value for a slider
  private double midiPressureToPressure(Slider slider, int midiPressure) {
    double max = slider.getMax();
    double min = slider.getMin();
    double range = max - min;
    return min + (midiPressure / MidiNote.MAX_PRESSURE) * range;
  }

  // handles newly received MIDI messages. For CC messages, this handles
  // whether or not there is a slider that is associated with the CC channel,
  // and the slider's value is updated if so. If there are channels that are
  // looking to be armed, a new association will be created between the CC
  // channel and the slider.
  @Override
  public void sendMidiMessage(ShortMessage message) {
    int command = message.getCommand();

    // the audioPlayer handles all non-CC MIDI messages
    if (command == ShortMessage.CONTROL_CHANGE) {
      int cc = message.getData1();
      int value = message.getData2();

      // if a user has selected a MIDI logo next to a slider, create a mapping
      // between the MIDI channel and the SVG MIDI logo
      if (armedMidi != null) {
        mapMidiCC(cc, armedMidi);
        armedMidiPaint = null;
        armedMidi = null;
      }
      // If there is a slider associated with the MIDI channel, update the value
      // of it
      if (CCMap.containsKey(cc)) {
        Slider slider = midiButtonMap.get(CCMap.get(cc));
        double sliderValue = midiPressureToPressure(slider, value);

        if (slider.isSnapToTicks()) {
          double increment = slider.getMajorTickUnit() / (slider.getMinorTickCount() + 1);
          sliderValue = increment * (Math.round(sliderValue / increment));
        }
        slider.setValue(sliderValue);
      }
    } else if (command == ShortMessage.PROGRAM_CHANGE) {
      // We want to change the file that is currently playing
      Platform.runLater(() -> changeFrameSource(message.getMessage()[1]));
    }
  }

  // must be functions, otherwise they are not initialised
  private List<CheckBox> checkBoxes() {
    return List.of(vectorCancellingCheckBox, bitCrushCheckBox, verticalDistortCheckBox,
      horizontalDistortCheckBox, wobbleCheckBox, smoothCheckBox, traceCheckBox);
  }
  private List<Slider> checkBoxSliders() {
    return List.of(vectorCancellingSlider, bitCrushSlider, verticalDistortSlider,
      horizontalDistortSlider, wobbleSlider, smoothSlider, traceSlider);
  }
  private List<String> checkBoxLabels() {
    return List.of("vectorCancelling", "bitCrush", "verticalDistort", "horizontalDistort",
      "wobble", "smooth", "trace");
  }
  private List<Slider> otherSliders() {
    return List.of(octaveSlider, frequencySlider, rotateSpeedSlider, translationSpeedSlider,
      volumeSlider, visibilitySlider, focalLengthSlider, objectRotateSpeedSlider);
  }
  private List<String> otherLabels() {
    return List.of("octave", "frequency", "rotateSpeed", "translationSpeed", "volume",
      "visibility", "focalLength", "objectRotateSpeed");
  }
  private List<Slider> allSliders() {
    List<Slider> sliders = new ArrayList<>(checkBoxSliders());
    sliders.addAll(otherSliders());
    return sliders;
  }
  private List<String> allLabels() {
    List<String> labels = new ArrayList<>(checkBoxLabels());
    labels.addAll(otherLabels());
    return labels;
  }

  private void appendSliders(List<Slider> sliders, List<String> labels, Element root, Document document) {
    for (int i = 0; i < sliders.size(); i++) {
      Element sliderElement = document.createElement(labels.get(i));
      sliderElement.appendChild(
        document.createTextNode(sliders.get(i).valueProperty().getValue().toString())
      );
      root.appendChild(sliderElement);
    }
  }

  private void loadSliderValues(List<Slider> sliders, List<String> labels, Element root) {
    for (int i = 0; i < sliders.size(); i++) {
      String value = root.getElementsByTagName(labels.get(i)).item(0).getTextContent();
      sliders.get(i).setValue(Float.parseFloat(value));
    }
  }

  private void saveProject(String projectFileName) {
    try {
      DocumentBuilderFactory documentFactory = DocumentBuilderFactory.newInstance();
      DocumentBuilder documentBuilder = documentFactory.newDocumentBuilder();
      Document document = documentBuilder.newDocument();

      Element root = document.createElement("project");
      document.appendChild(root);

      // Is there a nicer way of doing this?!
      List<CheckBox> checkBoxes = checkBoxes();
      List<Slider> checkBoxSliders = checkBoxSliders();
      List<String> checkBoxLabels = checkBoxLabels();
      List<Slider> otherSliders = otherSliders();
      List<String> otherLabels = otherLabels();
      List<Slider> sliders = allSliders();
      List<String> labels = allLabels();

      Element slidersElement = document.createElement("sliders");
      appendSliders(checkBoxSliders, checkBoxLabels, slidersElement, document);
      appendSliders(otherSliders, otherLabels, slidersElement, document);
      root.appendChild(slidersElement);

      Element checkBoxesElement = document.createElement("checkBoxes");
      for (int i = 0; i < checkBoxes.size(); i++) {
        Element checkBox = document.createElement(checkBoxLabels.get(i));
        checkBox.appendChild(
          document.createTextNode(checkBoxes.get(i).selectedProperty().getValue().toString())
        );
        checkBoxesElement.appendChild(checkBox);
      }
      root.appendChild(checkBoxesElement);

      Element midiElement = document.createElement("midi");
      for (Map.Entry<Integer, SVGPath> entry : CCMap.entrySet()) {
        Slider slider = midiButtonMap.get(entry.getValue());
        int index = sliders.indexOf(slider);
        Integer cc = entry.getKey();
        Element midiChannel = document.createElement(labels.get(index));
        midiChannel.appendChild(document.createTextNode(cc.toString()));
        midiElement.appendChild(midiChannel);
      }
      root.appendChild(midiElement);

      Element translationElement = document.createElement("translation");
      Element translationXElement = document.createElement("x");
      translationXElement.appendChild(document.createTextNode(translationXTextField.getText()));
      Element translationYElement = document.createElement("y");
      translationYElement.appendChild(document.createTextNode(translationYTextField.getText()));
      translationElement.appendChild(translationXElement);
      translationElement.appendChild(translationYElement);
      root.appendChild(translationElement);

      Element objectRotationElement = document.createElement("objectRotation");
      Element objectRotationXElement = document.createElement("x");
      objectRotationXElement.appendChild(document.createTextNode(Double.toString(rotation.getX())));
      Element objectRotationYElement = document.createElement("y");
      objectRotationYElement.appendChild(document.createTextNode(Double.toString(rotation.getY())));
      Element objectRotationZElement = document.createElement("z");
      objectRotationZElement.appendChild(document.createTextNode(Double.toString(rotation.getZ())));
      objectRotationElement.appendChild(objectRotationXElement);
      objectRotationElement.appendChild(objectRotationYElement);
      objectRotationElement.appendChild(objectRotationZElement);
      root.appendChild(objectRotationElement);

      Element filesElement = document.createElement("files");
      for (int i = 0; i < openFiles.size(); i++) {
        Element fileElement = document.createElement("file");
        Element fileNameElement = document.createElement("name");
        fileNameElement.appendChild(document.createTextNode(frameSourcePaths.get(i)));
        Element dataElement = document.createElement("data");
        String encodedData = Base64.getEncoder().encodeToString(openFiles.get(i));
        dataElement.appendChild(document.createTextNode(encodedData));
        fileElement.appendChild(fileNameElement);
        fileElement.appendChild(dataElement);
        filesElement.appendChild(fileElement);
      }
      root.appendChild(filesElement);

      TransformerFactory transformerFactory = TransformerFactory.newInstance();
      Transformer transformer = transformerFactory.newTransformer();
      DOMSource domSource = new DOMSource(document);
      StreamResult streamResult = new StreamResult(new File(projectFileName));

      transformer.transform(domSource, streamResult);
      openProjectPath = projectFileName;
    } catch (ParserConfigurationException | TransformerException e) {
      e.printStackTrace();
    }
  }

  private void resetCCMap() {
    armedMidi = null;
    armedMidiPaint = null;
    CCMap.clear();
    midiButtonMap.keySet().forEach(button -> button.setFill(Color.WHITE));
  }

  private void openProject(String projectFileName) {
    try {
      DocumentBuilderFactory documentFactory = DocumentBuilderFactory.newInstance();
      documentFactory.setFeature(XMLConstants.FEATURE_SECURE_PROCESSING, true);
      DocumentBuilder documentBuilder = documentFactory.newDocumentBuilder();

      Document document = documentBuilder.parse(new File(projectFileName));
      document.getDocumentElement().normalize();

      List<CheckBox> checkBoxes = checkBoxes();
      List<Slider> checkBoxSliders = checkBoxSliders();
      List<String> checkBoxLabels = checkBoxLabels();
      List<Slider> otherSliders = otherSliders();
      List<String> otherLabels = otherLabels();
      List<Slider> sliders = allSliders();
      List<String> labels = allLabels();

      Element root = document.getDocumentElement();
      Element slidersElement = (Element) root.getElementsByTagName("sliders").item(0);
      loadSliderValues(checkBoxSliders, checkBoxLabels, slidersElement);
      loadSliderValues(otherSliders, otherLabels, slidersElement);

      Element checkBoxesElement = (Element) root.getElementsByTagName("checkBoxes").item(0);
      for (int i = 0; i < checkBoxes.size(); i++) {
        String value = checkBoxesElement.getElementsByTagName(checkBoxLabels.get(i)).item(0).getTextContent();
        checkBoxes.get(i).setSelected(Boolean.parseBoolean(value));
      }

      Element midiElement = (Element) root.getElementsByTagName("midi").item(0);
      resetCCMap();
      for (int i = 0; i < labels.size(); i++) {
        NodeList elements = midiElement.getElementsByTagName(labels.get(i));
        if (elements.getLength() > 0) {
          Element midi = (Element) elements.item(0);
          int cc = Integer.parseInt(midi.getTextContent());
          Slider slider = sliders.get(i);
          SVGPath midiButton = midiButtonMap.entrySet().stream()
            .filter(entry -> entry.getValue() == slider)
            .findFirst().orElseThrow().getKey();
          mapMidiCC(cc, midiButton);
        }
      }
      root.appendChild(midiElement);

      Element translationElement = (Element) root.getElementsByTagName("translation").item(0);
      Element translationXElement = (Element) translationElement.getElementsByTagName("x").item(0);
      Element translationYElement = (Element) translationElement.getElementsByTagName("y").item(0);
      translationXTextField.setText(translationXElement.getTextContent());
      translationYTextField.setText(translationYElement.getTextContent());

      Element objectRotationElement = (Element) root.getElementsByTagName("objectRotation").item(0);
      Element objectRotationXElement = (Element) objectRotationElement.getElementsByTagName("x").item(0);
      Element objectRotationYElement = (Element) objectRotationElement.getElementsByTagName("y").item(0);
      Element objectRotationZElement = (Element) objectRotationElement.getElementsByTagName("z").item(0);
      rotation = new Vector3(
        Double.parseDouble(objectRotationXElement.getTextContent()),
        Double.parseDouble(objectRotationYElement.getTextContent()),
        Double.parseDouble(objectRotationZElement.getTextContent())
      );
      setObjRotate(rotation);

      Element filesElement = (Element) root.getElementsByTagName("files").item(0);
      List<byte[]> files = new ArrayList<>();
      List<String> fileNames = new ArrayList<>();
      NodeList fileElements = filesElement.getElementsByTagName("file");
      for (int i = 0; i < fileElements.getLength(); i++) {
        Element fileElement = (Element) fileElements.item(i);
        String fileData = fileElement.getElementsByTagName("data").item(0).getTextContent();
        files.add(Base64.getDecoder().decode(fileData));
        String fileName = fileElement.getElementsByTagName("name").item(0).getTextContent();
        fileNames.add(fileName);
      }
      updateFiles(files, fileNames);
      openProjectPath = projectFileName;
    } catch (ParserConfigurationException | SAXException | IOException e) {
      e.printStackTrace();
    }
  }
}
