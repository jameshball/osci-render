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
import sh.ball.audio.*;
import sh.ball.audio.effect.*;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
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
import javax.xml.parsers.ParserConfigurationException;

import org.xml.sax.SAXException;
import sh.ball.audio.effect.Effect;
import sh.ball.audio.effect.EffectType;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.ConglomerateAudioEngine;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.audio.midi.MidiListener;
import sh.ball.audio.midi.MidiNote;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.ObjFrameSettings;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import static sh.ball.math.Math.round;
import static sh.ball.math.Math.tryParse;

public class Controller implements Initializable, FrequencyListener, MidiListener {

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
  private final List<String> frameSourcePaths = new ArrayList<>();
  private List<FrameSource<List<Shape>>> frameSources = new ArrayList<>();
  private FrameProducer<List<Shape>> producer;
  private int currentFrameSource;
  private Vector3 rotation;

  // javafx
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

  public Controller() throws Exception {
    MidiCommunicator midiCommunicator = new MidiCommunicator();
    midiCommunicator.addListener(this);
    new Thread(midiCommunicator).start();

    this.audioPlayer = new ShapeAudioPlayer(ConglomerateAudioEngine::new, midiCommunicator);
    FrameSource<List<Shape>> frames = new ObjParser(DEFAULT_OBJ).parse();
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

  // selects a new file or folder for files to be rendered from
  private void chooseFile(File chosenFile) {
    try {
      if (chosenFile.exists()) {
        List<FrameSource<List<Shape>>> oldFrameSources = frameSources;
        frameSources = new ArrayList<>();
        frameSourcePaths.clear();

        if (chosenFile.isDirectory()) {
          jkLabel.setVisible(true);
          for (File file : Objects.requireNonNull(chosenFile.listFiles())) {
            try {
              frameSources.add(ParserFactory.getParser(file.getAbsolutePath()).parse());
              frameSourcePaths.add(file.getName());
            } catch (IOException ignored) {
            }
          }
        } else {
          jkLabel.setVisible(false);
          frameSources.add(ParserFactory.getParser(chosenFile.getAbsolutePath()).parse());
          frameSourcePaths.add(chosenFile.getName());
        }

        oldFrameSources.forEach(FrameSource::disable);
        changeFrameSource(0);
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
    int index = currentFrameSource + 1;
    if (index >= frameSources.size()) {
      index = 0;
    }
    changeFrameSource(index);
  }

  // decrements and changes the frameSource after pressing 'k'
  public void previousFrameSource() {
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
      int id = message.getData1();
      int value = message.getData2();

      // if a user has selected a MIDI logo next to a slider, create a mapping
      // between the MIDI channel and the SVG MIDI logo
      if (armedMidi != null) {
        if (CCMap.containsValue(armedMidi)) {
          CCMap.values().remove(armedMidi);
        }
        if (CCMap.containsKey(id)) {
          CCMap.get(id).setFill(Color.WHITE);
        }
        CCMap.put(id, armedMidi);
        armedMidi.setFill(Color.LIME);
        armedMidiPaint = null;
        armedMidi = null;
      }
      // If there is a slider associated with the MIDI channel, update the value
      // of it
      if (CCMap.containsKey(id)) {
        Slider slider = midiButtonMap.get(CCMap.get(id));
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
}
