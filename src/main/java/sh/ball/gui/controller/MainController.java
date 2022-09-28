package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.collections.FXCollections;
import javafx.scene.control.*;
import javafx.scene.control.Menu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.TextField;
import javafx.scene.paint.Color;
import javafx.scene.paint.Paint;
import javafx.scene.shape.SVGPath;
import javafx.util.Duration;
import javafx.util.converter.IntegerStringConverter;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;
import sh.ball.audio.*;

import java.awt.*;
import java.io.*;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.text.*;
import java.util.*;
import java.util.List;
import java.util.concurrent.*;
import java.util.function.Consumer;
import java.util.function.UnaryOperator;
import java.util.logging.Level;
import java.util.stream.Collectors;

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
import javax.xml.transform.Transformer;
import javax.xml.transform.TransformerFactory;
import javax.xml.transform.dom.DOMSource;
import javax.xml.transform.stream.StreamResult;

import sh.ball.audio.effect.AnimationType;
import sh.ball.audio.engine.*;
import sh.ball.audio.midi.MidiListener;
import sh.ball.audio.midi.MidiNote;
import sh.ball.engine.ObjectServer;
import sh.ball.engine.ObjectSet;
import sh.ball.gui.Gui;
import sh.ball.gui.components.EffectComponentGroup;
import sh.ball.oscilloscope.ByteWebSocketServer;
import sh.ball.parser.FileParser;
import sh.ball.parser.lua.LuaParser;
import sh.ball.parser.obj.ObjFrameSettings;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.parser.svg.SvgParser;
import sh.ball.parser.txt.FontStyle;
import sh.ball.shapes.Shape;
import sh.ball.shapes.ShapeFrameSource;
import sh.ball.shapes.Vector2;

import static sh.ball.gui.Gui.*;
import static sh.ball.math.Math.parseable;

public class MainController implements Initializable, FrequencyListener, MidiListener, AudioInputListener {

  private static final double MIN_MIC_VOLUME = 0.025;
  // need a set a locale, otherwise there is inconsistent casting to/from doubles/strings
  public static final DecimalFormat FORMAT = new DecimalFormat("0.00", new DecimalFormatSymbols(Locale.UK));
  private static final double BLOCK_INCREMENT = 0.005;
  private static final double MAJOR_TICK_UNIT = 0.1;
  private static final long MAX_RECENT_FILES = 15;

  private Consumer<String> addRecentFile;
  private Consumer<String> removeRecentFile;
  private String openProjectPath;
  private final FileChooser wavFileChooser = new FileChooser();

  private ObjectServer objectServer;

  // audio
  private int sampleRate;
  private FrequencyAnalyser<List<Shape>> analyser;
  private final double[] micSamples = new double[64];
  private int micSampleIndex = 0;
  private double[] targetSliderValue;
  private boolean recording = false;
  private Timeline recordingTimeline;

  // midi
  private final Map<Integer, SVGPath> CCMap = new HashMap<>();
  private Paint armedMidiPaint;
  private SVGPath armedMidi;
  private Map<SVGPath, Slider> midiButtonMap;
  private final Map<Slider, Short> channelClosestToZero = new HashMap<>();
  private int midiDeadzone = 5;

  // frames
  private static final InputStream DEFAULT_OBJ = MainController.class.getResourceAsStream("/models/cube.obj");
  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  private final Set<String> unsavedFileNames = new LinkedHashSet<>();
  private List<byte[]> openFiles = new ArrayList<>();
  private List<String> frameSourcePaths = new ArrayList<>();
  private List<FileParser<FrameSource<Vector2>>> sampleParsers = new ArrayList<>();
  private List<FrameSource<List<Shape>>> frameSources = new ArrayList<>();
  private FrameProducer<List<Shape>> producer;
  private int currentFrameSource;
  private boolean objectServerRendering = false;

  // javafx
  private Callable<List<String>> recentFiles;
  private final FileChooser osciFileChooser = new FileChooser();
  private Stage stage;

  // software oscilloscope
  private static final int SOSCI_NUM_VERTICES = 4096;
  private static final int SOSCI_VERTEX_SIZE = 2;
  private static final int FRAME_SIZE = 2;
  private byte[] buffer;
  private ByteWebSocketServer webSocketServer;

  @FXML
  private EffectsController effectsController;
  @FXML
  private ObjController objController;
  @FXML
  private LuaController luaController;
  @FXML
  private ImageController imageController;
  @FXML
  private GeneralController generalController;
  @FXML
  private VolumeController volumeController;
  @FXML
  private TitledPane objTitledPane;
  @FXML
  private TitledPane luaTitledPane;
  @FXML
  private MenuItem openProjectMenuItem;
  @FXML
  private Menu audioMenu;
  @FXML
  private Menu recentProjectMenu;
  @FXML
  private MenuItem saveProjectMenuItem;
  @FXML
  private MenuItem saveAsProjectMenuItem;
  @FXML
  private MenuItem resetMidiMappingMenuItem;
  @FXML
  private MenuItem stopMidiNotesMenuItem;
  @FXML
  private MenuItem resetMidiMenuItem;
  @FXML
  private CheckMenuItem flipXCheckMenuItem;
  @FXML
  private CheckMenuItem flipYCheckMenuItem;
  @FXML
  private CheckMenuItem hideHiddenMeshesCheckMenuItem;
  @FXML
  private CheckMenuItem renderUsingGpuCheckMenuItem;
  @FXML
  private ListView<String> fontFamilyListView;
  @FXML
  private ComboBox<FontStyle> fontStyleComboBox;
  @FXML
  private Spinner<Integer> midiChannelSpinner;
  @FXML
  private Spinner<Double> attackSpinner;
  @FXML
  private Spinner<Double> decaySpinner;
  @FXML
  private ComboBox<PrintableSlider> sliderComboBox;
  @FXML
  private TextField sliderMinTextField;
  @FXML
  private TextField sliderMaxTextField;
  @FXML
  private Spinner<Integer> deadzoneSpinner;
  @FXML
  private ListView<AudioDevice> deviceListView;
  @FXML
  private CustomMenuItem audioDeviceMenuItem;
  @FXML
  private Slider brightnessSlider;
  @FXML
  private CustomMenuItem recordLengthMenuItem;
  @FXML
  private CheckBox recordCheckBox;
  @FXML
  private MenuItem recordMenuItem;
  @FXML
  private Spinner<Double> recordLengthSpinner;
  @FXML
  private MenuItem softwareOscilloscopeMenuItem;
  @FXML
  private ComboBox<AudioSample> audioSampleComboBox;

  // initialises midiButtonMap by mapping MIDI logo SVGs to the slider that they
  // control if they are selected.
  private Map<SVGPath, Slider> initializeMidiButtonMap() {
    Map<SVGPath, Slider> midiMap = new HashMap<>();
    subControllers().forEach(controller -> midiMap.putAll(controller.getMidiButtonMap()));
    return midiMap;
  }

  private List<SubController> subControllers() {
    return List.of(effectsController, objController, imageController, generalController, luaController, volumeController);
  }

  private void updateSliderUnits(Slider slider) {
    slider.setBlockIncrement(BLOCK_INCREMENT * (slider.getMax() - slider.getMin()));
    slider.setMajorTickUnit(Math.max(sh.ball.math.Math.EPSILON, MAJOR_TICK_UNIT * (slider.getMax() - slider.getMin())));
  }

  private void updateClosestChannelToZero(Slider slider) {
    short closestToZero = 0;
    double closestValue = Double.MAX_VALUE;
    for (short i = 0; i <= MidiNote.MAX_VELOCITY; i++) {
      double value = getValueInSliderRange(slider, i / (float) MidiNote.MAX_VELOCITY);
      if (Math.abs(value) < closestValue) {
        closestValue = Math.abs(value);
        closestToZero = i;
      }
    }
    channelClosestToZero.put(slider, closestToZero);
  }

  public void shutdown() {
    if (webSocketServer != null) {
      webSocketServer.shutdown();
    }
  }

  // alternates between recording and not recording when called.
  // If it is a non-timed recording, it is saved when this is called and
  // recording is stopped. If it is a time recording, this function will cancel
  // the recording.
  private void toggleRecord() {
    recording = !recording;
    audioSampleComboBox.setDisable(recording);
    deviceListView.setDisable(recording);
    boolean timedRecord = recordCheckBox.isSelected();
    if (recording) {
      // if it is a timed recording then a timeline is scheduled to start and
      // stop recording at the predefined times.
      if (timedRecord) {
        double recordingLength;
        try {
          recordingLength = recordLengthSpinner.getValue();
        } catch (NumberFormatException e) {
          generalController.setRecordResult("Please set a valid record length");
          recording = false;
          return;
        }
        recordMenuItem.setText("Cancel");
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
            Platform.runLater(() -> {
              audioSampleComboBox.setDisable(false);
              deviceListView.setDisable(false);
            });
          }
        );
        recordingTimeline = new Timeline(kf1, kf2);
        Platform.runLater(recordingTimeline::play);
      } else {
        recordMenuItem.setText("Stop Recording");
        audioPlayer.startRecord();
      }
      generalController.setRecordResult("Recording...");
    } else if (timedRecord) {
      // cancel the recording
      recordingTimeline.stop();
      generalController.setRecordResult("");
      recordMenuItem.setText("Start Recording");
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
      recordMenuItem.setText("Record");
      AudioInputStream input = audioPlayer.stopRecord();
      File file = wavFileChooser.showSaveDialog(stage);
      if (file != null) {
        updateLastVisitedDirectory(new File(file.getParent()));
      }
      SimpleDateFormat formatter = new SimpleDateFormat("yyyy-MM-dd_HH-mm-ss");
      Date date = new Date(System.currentTimeMillis());
      if (file == null) {
        file = new File("out-" + formatter.format(date) + ".wav");
      }
      AudioSystem.write(input, AudioFileFormat.Type.WAVE, file);
      input.close();
      generalController.setRecordResult("Saved to " + file.getAbsolutePath());
    } catch (IOException e) {
      generalController.setRecordResult("Error saving file");
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    List<Slider> sliders = sliders();
    List<BooleanProperty> micCheckBoxes = micSelected();
    targetSliderValue = new double[sliders.size()];
    for (int i = 0; i < sliders.size(); i++) {
      updateClosestChannelToZero(sliders.get(i));
      targetSliderValue[i] = sliders.get(i).getValue();
      int finalI = i;
      if (micCheckBoxes.get(i) != null) {
        BooleanProperty selected = micCheckBoxes.get(i);
        sliders.get(i).valueProperty().addListener((e, old, value) -> {
          if (!selected.getValue()) {
            targetSliderValue[finalI] = value.doubleValue();
          }
        });
      }
      sliders.get(i).minProperty().addListener(e -> updateClosestChannelToZero(sliders.get(finalI)));
      sliders.get(i).maxProperty().addListener(e -> updateClosestChannelToZero(sliders.get(finalI)));
      sliders.get(i).setOnMouseClicked(event -> {
        if (event.getClickCount() == 2) {
          sliders.get(finalI).setValue(0);
        }
      });
    }

    generalController.setMainController(this);
    luaController.setMainController(this);

    this.midiButtonMap = initializeMidiButtonMap();

    midiButtonMap.keySet().forEach(midi -> {
      midi.setOnMouseClicked(e -> {
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
      });
    });

    osciFileChooser.setInitialFileName("project.osci");
    osciFileChooser.getExtensionFilters().add(
      new FileChooser.ExtensionFilter("osci-render files", "*.osci")
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
        try {
          openProject(file.getAbsolutePath());
        } catch (Exception ex) {
          logger.log(Level.SEVERE, ex.getMessage(), ex);
        }
      }
    });

    resetMidiMappingMenuItem.setOnAction(e -> resetCCMap());
    stopMidiNotesMenuItem.setOnAction(e -> audioPlayer.stopMidiNotes());
    resetMidiMenuItem.setOnAction(e -> audioPlayer.resetMidi());

    flipXCheckMenuItem.selectedProperty().addListener((e, old, flip) -> audioPlayer.flipXChannel(flip));
    flipYCheckMenuItem.selectedProperty().addListener((e, old, flip) -> audioPlayer.flipYChannel(flip));

    hideHiddenMeshesCheckMenuItem.selectedProperty().addListener((e, old, hidden) -> objController.hideHiddenMeshes(hidden));

    renderUsingGpuCheckMenuItem.selectedProperty().addListener((e, old, usingGpu) -> objController.renderUsingGpu(usingGpu));

    NumberFormat format = NumberFormat.getIntegerInstance();
    UnaryOperator<TextFormatter.Change> filter = c -> {
      if (c.isContentChange()) {
        ParsePosition parsePosition = new ParsePosition(0);
        // NumberFormat evaluates the beginning of the text
        format.parse(c.getControlNewText(), parsePosition);
        if (parsePosition.getIndex() == 0 ||
          parsePosition.getIndex() < c.getControlNewText().length()) {
          // reject parsing the complete text failed
          return null;
        }
      }
      return c;
    };

    midiChannelSpinner.setValueFactory(new SpinnerValueFactory.IntegerSpinnerValueFactory(0, MidiNote.MAX_CHANNEL));
    midiChannelSpinner.getEditor().setTextFormatter(new TextFormatter<>(new IntegerStringConverter(), 0, filter));
    midiChannelSpinner.valueProperty().addListener((o, oldValue, newValue) -> audioPlayer.setMainMidiChannel(newValue));


    deadzoneSpinner.setValueFactory(new SpinnerValueFactory.IntegerSpinnerValueFactory(0, 20, 5));
    deadzoneSpinner.valueProperty().addListener((e, old, deadzone) -> this.midiDeadzone = deadzone);

    List<PrintableSlider> printableSliders = new ArrayList<>();
    sliders.forEach(slider -> {
      // don't allow frequency or brightness slider to be changed
      if (!slider.getId().equals("frequencySlider") && !slider.getId().equals("brightnessSlider")) {
        printableSliders.add(new PrintableSlider(slider));
      }
    });
    sliderComboBox.setItems(FXCollections.observableList(printableSliders));
    sliderComboBox.valueProperty().addListener((e, old, slider) -> {
      sliderMinTextField.setText(FORMAT.format(slider.slider.getMin()));
      sliderMaxTextField.setText(FORMAT.format(slider.slider.getMax()));
    });
    sliderComboBox.setValue(printableSliders.get(0));

    String[] installedFonts = GraphicsEnvironment.getLocalGraphicsEnvironment().getAvailableFontFamilyNames();
    fontFamilyListView.setItems(FXCollections.observableList(Arrays.stream(installedFonts).toList()));
    if (fontFamilyListView.getItems().contains("SansSerif")) {
      fontFamilyListView.getSelectionModel().select("SansSerif");
    } else {
      fontFamilyListView.getSelectionModel().select(installedFonts[0]);
    }
    fontFamilyListView.getSelectionModel().selectedItemProperty().addListener((e, old, family) -> updateTextFiles());


    fontStyleComboBox.setItems(FXCollections.observableList(Arrays.stream(FontStyle.values()).toList()));
    fontStyleComboBox.setValue(FontStyle.PLAIN);
    fontStyleComboBox.valueProperty().addListener((e, old, family) -> updateTextFiles());

    sliderMinTextField.focusedProperty().addListener((e, old, focused) -> {
      String text = sliderMinTextField.getText();
      if (!focused && parseable(text)) {
        sliderComboBox.getValue().slider.setMin(Double.parseDouble(text));
        updateSliderUnits(sliderComboBox.getValue().slider);
      }
    });
    sliderMaxTextField.focusedProperty().addListener((e, old, focused) -> {
      String text = sliderMaxTextField.getText();
      if (!focused && parseable(text)) {
        sliderComboBox.getValue().slider.setMax(Double.parseDouble(text));
        updateSliderUnits(sliderComboBox.getValue().slider);
      }
    });

    attackSpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0, 10, 0.2, 0.01));
    attackSpinner.valueProperty().addListener((o, oldValue, newValue) -> audioPlayer.setAttack(newValue));

    decaySpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0, 10, 0.1, 0.01));
    decaySpinner.valueProperty().addListener((o, oldValue, newValue) -> audioPlayer.setDecay(newValue));

    recordLengthSpinner.setValueFactory(new SpinnerValueFactory.DoubleSpinnerValueFactory(0.1, 100000000, 2.0, 0.1));

    wavFileChooser.setInitialFileName("out.wav");
    wavFileChooser.getExtensionFilters().addAll(
      new FileChooser.ExtensionFilter("WAV Files", "*.wav"),
      new FileChooser.ExtensionFilter("All Files", "*.*")
    );

    recordMenuItem.setOnAction(event -> toggleRecord());

    recordCheckBox.selectedProperty().addListener((e, oldVal, newVal) -> {
      recordLengthMenuItem.setDisable(!newVal);
    });

    new Thread(() -> {
      List<AudioDevice> devices = audioPlayer.devices();
      Platform.runLater(() -> {
        deviceListView.setItems(FXCollections.observableList(devices));
        deviceListView.getSelectionModel().select(defaultDevice);
        deviceListView.getSelectionModel().selectedItemProperty().addListener((options, oldDevice, newDevice) -> {
          if (newDevice != null) {
            switchAudioDevice(newDevice);
          }
        });
      });
      switchAudioDevice(defaultDevice, false);
    }).start();

    audioSampleComboBox.setItems(FXCollections.observableList(List.of(AudioSample.UINT8, AudioSample.INT8, AudioSample.INT16, AudioSample.INT24, AudioSample.INT32)));
    audioSampleComboBox.setValue(AudioSample.INT16);
    audioSampleComboBox.valueProperty().addListener((options, oldSample, sample) -> {
      if (sample != null) {
        audioPlayer.setAudioSample(sample);
      }
    });

    brightnessSlider.valueProperty().addListener((e, old, brightness) -> audioPlayer.setBrightness(brightness.doubleValue()));
  }

  private void updateTextFiles() {
    List<Integer> textFrameSources = frameSourcePaths.stream()
      .filter(path -> path.endsWith(".txt"))
      .map(path -> frameSourcePaths.indexOf(path))
      .toList();

    textFrameSources.forEach(i -> updateFileData(openFiles.get(i), frameSourcePaths.get(i), false));
  }

  public void setLuaVariable(String variableName, Object value) {
    sampleParsers.forEach(sampleParser -> {
      if (sampleParser instanceof LuaParser luaParser) {
        luaParser.setVariable(variableName, value);
      }
    });
  }

  public void setSoftwareOscilloscopeAction(Runnable openBrowser) {
    softwareOscilloscopeMenuItem.setOnAction((e) -> openBrowser.run());
  }

  private void sendAudioDataToWebSocket(ByteWebSocketServer server) {
    while (true) {
      try {
        audioPlayer.read(buffer);
        server.send(buffer);
      } catch (InterruptedException e) {
        logger.log(Level.WARNING, e.getMessage(), e);
      }
    }
  }

  private void enableObjectServerRendering() {
    Platform.runLater(() -> {
      objectServerRendering = true;
      ObjectSet set = objectServer.getObjectSet();
      disableSources();
      audioPlayer.removeSampleSource();
      set.enable();

      producer = new FrameProducer<>(audioPlayer, set);
      objController.setAudioProducer(producer);

      executor.submit(producer);
      effectsController.restartEffects();

      generalController.setFrameSourceName("Rendering from external input");
      generalController.updateFrameLabels();
      objTitledPane.setDisable(true);
    });
  }

  private void disableObjectServerRendering() {
    Platform.runLater(() -> {
      objectServer.getObjectSet().disable();
      objectServerRendering = false;
      changeFrameSource(currentFrameSource);
    });
  }

  // used when a file is chosen so that the same folder is reopened when a
  // file chooser opens
  private void updateLastVisitedDirectory(File file) {
    String lastVisitedDirectory = file != null ? file.getAbsolutePath() : System.getProperty("user.home");
    File dir = new File(lastVisitedDirectory);
    wavFileChooser.setInitialDirectory(dir);
    osciFileChooser.setInitialDirectory(dir);
    generalController.updateLastVisitedDirectory(dir);
  }

  private void switchAudioDevice(AudioDevice device) {
    switchAudioDevice(device, true);
  }

  // restarts audioPlayer and FrequencyAnalyser to support new device
  private void switchAudioDevice(AudioDevice device, boolean reset) {
    if (reset) {
      try {
        audioPlayer.reset();
      } catch (Exception e) {
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
    }
    audioPlayer.setDevice(device);
    audioPlayer.setBrightness(brightnessSlider.getValue());
    effectsController.setAudioDevice(device);
    sampleRate = device.sampleRate();
    // brightness is configurable for many-output audio interfaces
    if (device.channels() > 2) {
      Platform.runLater(() -> brightnessSlider.setDisable(false));
    }
    if (analyser != null) {
      analyser.stop();
    }
    analyser = new FrequencyAnalyser<>(audioPlayer, 2, sampleRate);
    startFrequencyAnalyser(analyser);
    effectsController.setFrequencyAnalyser(analyser);
    startAudioPlayerThread();
  }

  // creates a new thread for the audioPlayer and starts it
  private void startAudioPlayerThread() {
    Thread audioPlayerThread = new Thread(audioPlayer);
    audioPlayerThread.setUncaughtExceptionHandler((thread, throwable) -> throwable.printStackTrace());
    audioPlayerThread.setPriority(Thread.MAX_PRIORITY);
    audioPlayerThread.start();
  }

  // creates a new thread for the frequency analyser and adds the wobble effect and the controller
  // as listeners of it so that they can get updates as the frequency changes
  private void startFrequencyAnalyser(FrequencyAnalyser<List<Shape>> analyser) {
    analyser.addListener(this);
    effectsController.setFrequencyAnalyser(analyser);
    new Thread(analyser).start();
  }

  private void disableSources() {
    frameSources.stream().filter(Objects::nonNull).forEach(FrameSource::disable);
    sampleParsers.stream().filter(Objects::nonNull).forEach(parser -> {
      try {
        parser.get().disable();
      } catch (Exception e) {
        logger.log(Level.SEVERE, e.getMessage(), e);
      }
    });
  }

  // changes the FrameProducer e.g. could be changing from a 3D object to an
  // SVG. The old FrameProducer is stopped and a new one created and initialised
  // with the same settings that the original had.
  private void changeFrameSource(int index) {
    if (frameSources.size() == 0) {
      Platform.runLater(() -> {
        generalController.setFrameSourceName("No file open!");
        generalController.updateFrameLabels();
      });
      audioPlayer.removeSampleSource();
      audioPlayer.addFrame(List.of(new Vector2()));
      return;
    }
    index = Math.max(0, Math.min(index, frameSources.size() - 1));
    currentFrameSource = index;
    disableSources();

    FrameSource<List<Shape>> frames = frameSources.get(index);
    FrameSource<Vector2> samples = null;
    try {
      FileParser<FrameSource<Vector2>> sampleParser = sampleParsers.get(index);
      if (sampleParser != null ) {
        samples = sampleParser.get();
      }
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
    if (frames != null) {
      frames.enable();
      audioPlayer.removeSampleSource();

      Object oldSettings = producer.getFrameSettings();
      producer = new FrameProducer<>(audioPlayer, frames);
      objController.setAudioProducer(producer);

      // Apply the same settings that the previous frameSource had
      objController.updateObjectRotateSpeed();
      objController.updateFocalLength();
      if (oldSettings instanceof ObjFrameSettings settings) {
        objController.setObjRotate(settings.baseRotation, settings.currentRotation);
      }
      // FIXME: Is this safe since we are accessing GUI object in non-GUI thread?
      objController.hideHiddenMeshes(hideHiddenMeshesCheckMenuItem.isSelected());
      objController.renderUsingGpu(renderUsingGpuCheckMenuItem.isSelected());
      executor.submit(producer);
    } else if (samples != null) {
      samples.enable();
      audioPlayer.setSampleSource(samples);
    } else {
      throw new RuntimeException("Expected to have either a frame source or sample source");
    }
    effectsController.restartEffects();

    String fileName = frameSourcePaths.get(index);

    Platform.runLater(() -> {
      generalController.setFrameSourceName(frameSourcePaths.get(currentFrameSource));
      generalController.updateFrameLabels();
      if (ObjParser.isObjFile(fileName)) {
        objTitledPane.setVisible(true);
        objTitledPane.setDisable(false);
        luaTitledPane.setVisible(false);
      } else if (LuaParser.isLuaFile(fileName)) {
        objTitledPane.setVisible(false);
        luaTitledPane.setVisible(true);
      } else {
        objTitledPane.setVisible(true);
        objTitledPane.setDisable(true);
        luaTitledPane.setVisible(false);
      }
    });
  }

  private void createFile(String name, byte[] fileData, List<FileParser<FrameSource<Vector2>>> sampleParsers, List<FrameSource<List<Shape>>> frameSources, List<String> frameSourcePaths, List<byte[]> openFiles) throws Exception {
    if (frameSourcePaths.contains(name)) {
      throw new IOException("File already exists with this name");
    }
    if (LuaParser.isLuaFile(name)) {
      LuaParser luaParser = new LuaParser();
      luaParser.setScriptFromInputStream(new ByteArrayInputStream(fileData));
      luaParser.parse();
      sampleParsers.add(luaParser);
      Platform.runLater(() -> luaController.updateLuaVariables());
      frameSources.add(null);
    } else {
      frameSources.add(ParserFactory.getParser(name, fileData, fontFamilyListView.getSelectionModel().getSelectedItem(), fontStyleComboBox.getValue()).parse());
      sampleParsers.add(null);
    }
    frameSourcePaths.add(name);
    openFiles.add(fileData);
    Platform.runLater(() -> {
      generalController.showMultiFileTooltip(frameSources.size() > 1);
      if (generalController.framesPlaying() && frameSources.size() == 1) {
        generalController.disablePlayback();
      }
    });
  }

  public void createFile(String name, byte[] fileData) throws Exception {
    createFile(name, fileData, sampleParsers, frameSources, frameSourcePaths, openFiles);
    if (!objectServerRendering) {
      changeFrameSource(frameSources.size() - 1);
    }
    unsavedFileNames.add(name);
    setUnsavedFileWarning();
  }

  public void deleteCurrentFile() {
    if (frameSources.isEmpty()) {
      return;
    }
    disableSources();
    sampleParsers.remove(currentFrameSource);
    frameSources.remove(currentFrameSource);
    String name = frameSourcePaths.get(currentFrameSource);
    unsavedFileNames.remove(name);
    setUnsavedFileWarning();
    frameSourcePaths.remove(currentFrameSource);
    openFiles.remove(currentFrameSource);
    if (currentFrameSource >= frameSources.size()) {
      currentFrameSource--;
    }
    if (!objectServerRendering) {
      changeFrameSource(currentFrameSource);
    }
    Platform.runLater(() -> generalController.showMultiFileTooltip(frameSources.size() > 1));
  }

  public void updateFileData(byte[] file, String name, boolean updateUnsavedFiles) {
    int index = frameSourcePaths.indexOf(name);
    if (index == -1) {
      throw new RuntimeException("Can't find open file with name: " + name);
    }
    frameSourcePaths.set(index, name);
    openFiles.set(index, file);
    try {
      if (LuaParser.isLuaFile(name)) {
        FileParser<FrameSource<Vector2>> sampleParser = sampleParsers.get(index);
        if (sampleParser instanceof LuaParser luaParser) {
          luaParser.setScriptFromInputStream(new ByteArrayInputStream(file));
        }
        sampleParser.parse().disable();
        sampleParsers.set(index, sampleParser);
        frameSources.set(index, null);
      } else {
        FrameSource<List<Shape>> frameSource = frameSources.get(index);
        frameSources.set(index, ParserFactory.getParser(name, file, fontFamilyListView.getSelectionModel().getSelectedItem(), fontStyleComboBox.getValue()).parse());
        frameSource.disable();
        sampleParsers.set(index, null);
      }

      if (updateUnsavedFiles) {
        unsavedFileNames.add(name);
        setUnsavedFileWarning();
      }
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }

    if (index == currentFrameSource) {
      changeFrameSource(index);
    }
  }

  void setUnsavedFileWarning() {
    Platform.runLater(() -> {
      if (!unsavedFileNames.isEmpty()) {
        String warning = "(unsaved files: " + String.join(", ", unsavedFileNames) + ")";
        updateTitle(warning, openProjectPath);
      } else {
        updateTitle(null, openProjectPath);
      }
    });
  }

  void updateFiles(List<byte[]> files, List<String> names, int startingFrameSource) throws Exception {
    List<FileParser<FrameSource<Vector2>>> newSampleParsers = new ArrayList<>();
    List<FrameSource<List<Shape>>> newFrameSources = new ArrayList<>();
    List<String> newFrameSourcePaths = new ArrayList<>();
    List<byte[]> newOpenFiles = new ArrayList<>();

    unsavedFileNames.clear();
    setUnsavedFileWarning();

    Platform.runLater(() -> {
      generalController.setFrameSourceName("Loading file" + (names.size() > 1 ? "s" : "") + " - audio may stutter!");
      generalController.updateFrameLabels();
    });

    for (int i = 0; i < files.size(); i++) {
      try {
        createFile(names.get(i), files.get(i), newSampleParsers, newFrameSources, newFrameSourcePaths, newOpenFiles);
      } catch (Exception e) {
        logger.log(Level.WARNING, e.getMessage(), e);
        Platform.runLater(() -> {
          generalController.setFrameSourceName(e.getMessage());
          generalController.updateFrameLabels();
        });
      }
    }

    if (newFrameSources.size() > 0) {
      disableSources();
      sampleParsers = newSampleParsers;
      frameSources = newFrameSources;
      frameSourcePaths = newFrameSourcePaths;
      openFiles = newOpenFiles;
      changeFrameSource(startingFrameSource);
    }
  }

  // used so that the Controller has access to the stage, allowing it to open
  // file directories etc.
  public void setStage(Stage stage) {
    this.stage = stage;
    generalController.setStage(stage);
  }

  // increments and changes the frameSource after pressing 'j'
  public void nextFrameSource() {
    if (objectServerRendering || frameSources.size() == 1) {
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
    if (objectServerRendering || frameSources.size() == 1) {
      return;
    }
    int index = currentFrameSource - 1;
    if (index < 0) {
      index = frameSources.size() - 1;
    }
    changeFrameSource(index);
  }

  // determines whether the mouse is being used to rotate a 3D object
  public boolean mouseRotate() {
    return objController.mouseRotate();
  }

  // determines whether the mouse is being used to translate the screen
  public boolean mouseTranslate() {
    return effectsController.mouseTranslate();
  }

  // updates the screen translation
  public void setTranslation(Vector2 translation) {
    effectsController.setTranslation(translation);
  }

  // stops the mouse rotating the 3D object when ESC is pressed or checkbox is
  // unchecked
  public void disableMouseRotate() {
    objController.disableMouseRotate();
  }

  // stops the mouse translating the screen when ESC is pressed or checkbox is
  // unchecked
  public void disableMouseTranslate() {
    effectsController.disableMouseTranslate();
  }

  // updates the 3D object base and current rotation angle
  public void setMouseXY(double mouseX, double mouseY) {
    objController.setRotateXY(new Vector2(mouseX, mouseY));
  }

  @Override
  public void updateFrequency(double leftFrequency, double rightFrequency) {
    generalController.updateFrequency(leftFrequency, rightFrequency);
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

  private double getValueInSliderRange(Slider slider, double value) {
    double max = slider.getMax();
    double min = slider.getMin();
    double range = max - min;
    double sliderValue = min + value * range;
    if (slider.isSnapToTicks()) {
      double increment = slider.getMajorTickUnit() / (slider.getMinorTickCount() + 1);
      sliderValue = increment * (Math.round(sliderValue / increment));
    }
    return  sliderValue;
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
      if (cc <= MidiNote.MAX_CC && CCMap.containsKey(cc)) {
        Platform.runLater(() -> {
          Slider slider = midiButtonMap.get(CCMap.get(cc));
          short closestToZero = channelClosestToZero.get(slider);
          double sliderValue = getValueInSliderRange(slider, value / (float) MidiNote.MAX_VELOCITY);
          // deadzone
          if (value >= closestToZero - midiDeadzone && value <= closestToZero + midiDeadzone && sliderValue < 1) {
            slider.setValue(0);
          } else {
            int leftDeadzone = Math.min(closestToZero, midiDeadzone);
            int rightDeadzone = Math.min(MidiNote.MAX_VELOCITY - closestToZero, midiDeadzone);
            int actualChannels = MidiNote.MAX_VELOCITY - (leftDeadzone + 1 + rightDeadzone);
            int correctedValue;
            if (value > closestToZero) {
              correctedValue = value - midiDeadzone;
            } else {
              correctedValue = value + midiDeadzone;
            }

            double scale = MidiNote.MAX_VELOCITY / (double) actualChannels;
            double zeroPoint = closestToZero / (double) MidiNote.MAX_VELOCITY;
            slider.setValue(getValueInSliderRange(slider, scale * ((correctedValue / (double) MidiNote.MAX_VELOCITY) - zeroPoint) + zeroPoint));
          }
        });
      } else if (cc == MidiNote.ALL_NOTES_OFF) {
        audioPlayer.stopMidiNotes();
      }
    } else if (command == ShortMessage.PROGRAM_CHANGE) {
      // We want to change the file that is currently playing
      Platform.runLater(() -> changeFrameSource(message.getMessage()[1]));
    }
  }

  // must be functions, otherwise they are not initialised
  private List<BooleanProperty> micSelected() {
    List<BooleanProperty> checkBoxes = new ArrayList<>();
    subControllers().forEach(controller -> checkBoxes.addAll(controller.micSelected()));
    checkBoxes.add(null);
    return checkBoxes;
  }
  private List<Slider> sliders() {
    List<Slider> sliders = new ArrayList<>();
    subControllers().forEach(controller -> sliders.addAll(controller.sliders()));
    sliders.add(brightnessSlider);
    return sliders;
  }

  private List<EffectComponentGroup> effects() {
    return subControllers().stream().map(SubController::effects).flatMap(Collection::stream).toList();
  }

  private List<String> labels() {
    List<String> labels = new ArrayList<>();
    subControllers().forEach(controller -> labels.addAll(controller.labels()));
    labels.add("brightness");
    return labels;
  }

  private void appendSliders(List<Slider> sliders, List<String> labels, Element root, Document document) {
    for (int i = 0; i < sliders.size(); i++) {
      Element sliderElement = document.createElement(labels.get(i));
      Element value = document.createElement("value");
      Element min = document.createElement("min");
      Element max = document.createElement("max");
      value.appendChild(
        document.createTextNode(sliders.get(i).valueProperty().getValue().toString())
      );
      min.appendChild(
        document.createTextNode(sliders.get(i).minProperty().getValue().toString())
      );
      max.appendChild(
        document.createTextNode(sliders.get(i).maxProperty().getValue().toString())
      );
      sliderElement.appendChild(value);
      sliderElement.appendChild(min);
      sliderElement.appendChild(max);
      root.appendChild(sliderElement);
    }
  }

  private void loadSliderValues(List<Slider> sliders, List<String> labels, Element root) {
    for (int i = 0; i < sliders.size(); i++) {
      NodeList nodes = root.getElementsByTagName(labels.get(i));
      // backwards compatibility
      if (nodes.getLength() > 0) {
        Element sliderElement = (Element) nodes.item(0);
        Slider slider = sliders.get(i);
        String value;
        // backwards compatibility
        if (sliderElement.getChildNodes().getLength() == 1) {
          value = sliderElement.getTextContent();
        } else {
          value = sliderElement.getElementsByTagName("value").item(0).getTextContent();
          String min = sliderElement.getElementsByTagName("min").item(0).getTextContent();
          String max = sliderElement.getElementsByTagName("max").item(0).getTextContent();
          slider.setMin(Double.parseDouble(min));
          slider.setMax(Double.parseDouble(max));
          updateSliderUnits(slider);
        }

        slider.setValue(Double.parseDouble(value));
        targetSliderValue[i] = Double.parseDouble(value);
      }
    }

    sliderMinTextField.setText(String.valueOf(sliderComboBox.getValue().slider.getMin()));
    sliderMaxTextField.setText(String.valueOf(sliderComboBox.getValue().slider.getMax()));
  }

  private void appendMicCheckBoxes(List<BooleanProperty> checkBoxes, List<String> labels, Element root, Document document) {
    for (int i = 0; i < checkBoxes.size(); i++) {
      if (checkBoxes.get(i) != null) {
        Element checkBox = document.createElement(labels.get(i));
        checkBox.appendChild(
          document.createTextNode(String.valueOf(checkBoxes.get(i).getValue()))
        );
        root.appendChild(checkBox);
      }
    }
  }

  private void loadMicCheckBoxes(List<BooleanProperty> micSelected, List<String> labels, Element root) {
    for (int i = 0; i < micSelected.size(); i++) {
      NodeList nodes = root.getElementsByTagName(labels.get(i));
      if (nodes.getLength() > 0) {
        String value = nodes.item(0).getTextContent();
        BooleanProperty selected = micSelected.get(i);
        if (selected != null) {
          selected.setValue(Boolean.parseBoolean(value));
        }
      }
    }
  }

  private void saveProject(String projectFileName) {
    try {
      DocumentBuilderFactory documentFactory = DocumentBuilderFactory.newInstance();
      DocumentBuilder documentBuilder = documentFactory.newDocumentBuilder();
      Document document = documentBuilder.newDocument();

      Element root = document.createElement("project");
      document.appendChild(root);

      List<BooleanProperty> micSelected = micSelected();
      List<Slider> sliders = sliders();
      List<String> labels = labels();

      Element slidersElement = document.createElement("sliders");
      appendSliders(sliders, labels, slidersElement, document);
      root.appendChild(slidersElement);

      Element micCheckBoxesElement = document.createElement("micCheckBoxes");
      appendMicCheckBoxes(micSelected, labels, micCheckBoxesElement, document);
      root.appendChild(micCheckBoxesElement);

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

      subControllers().forEach(controller -> controller.save(document).forEach(root::appendChild));

      Element checkBoxes = document.createElement("checkBoxes");
      effects().forEach(effect -> effect.save(document).forEach(checkBoxes::appendChild));
      root.appendChild(checkBoxes);

      Element flipX = document.createElement("flipX");
      Element flipY = document.createElement("flipY");
      flipX.appendChild(document.createTextNode(String.valueOf(flipXCheckMenuItem.isSelected())));
      flipY.appendChild(document.createTextNode(String.valueOf(flipYCheckMenuItem.isSelected())));
      root.appendChild(flipX);
      root.appendChild(flipY);

      Element hiddenEdges = document.createElement("hiddenEdges");
      hiddenEdges.appendChild(document.createTextNode(String.valueOf(hideHiddenMeshesCheckMenuItem.isSelected())));
      root.appendChild(hiddenEdges);

      Element usingGpu = document.createElement("usingGpu");
      usingGpu.appendChild(document.createTextNode(String.valueOf(renderUsingGpuCheckMenuItem.isSelected())));
      root.appendChild(usingGpu);

      Element deadzone = document.createElement("deadzone");
      deadzone.appendChild(document.createTextNode(deadzoneSpinner.getValue().toString()));
      root.appendChild(deadzone);

      Element mainMidiChannel = document.createElement("mainMidiChannel");
      mainMidiChannel.appendChild(document.createTextNode(midiChannelSpinner.getValue().toString()));
      root.appendChild(mainMidiChannel);

      Element midiAttack = document.createElement("midiAttack");
      midiAttack.appendChild(document.createTextNode(attackSpinner.getValue().toString()));
      root.appendChild(midiAttack);

      Element midiDecay = document.createElement("midiDecay");
      midiDecay.appendChild(document.createTextNode(decaySpinner.getValue().toString()));
      root.appendChild(midiDecay);

      Element audioSample = document.createElement("audioSample");
      audioSample.appendChild(document.createTextNode(audioSampleComboBox.getValue().name()));
      root.appendChild(audioSample);

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

      Element frameSource = document.createElement("frameSource");
      frameSource.appendChild(document.createTextNode(String.valueOf(currentFrameSource)));
      root.appendChild(frameSource);

      Element fontFamily = document.createElement("fontFamily");
      fontFamily.appendChild(document.createTextNode(fontFamilyListView.getSelectionModel().getSelectedItem()));
      root.appendChild(fontFamily);

      Element fontStyle = document.createElement("fontStyle");
      fontStyle.appendChild(document.createTextNode(String.valueOf(fontStyleComboBox.getValue().style)));
      root.appendChild(fontStyle);

      TransformerFactory transformerFactory = TransformerFactory.newInstance();
      Transformer transformer = transformerFactory.newTransformer();
      DOMSource domSource = new DOMSource(document);
      StreamResult streamResult = new StreamResult(new File(projectFileName));

      transformer.transform(domSource, streamResult);
      openProjectPath = projectFileName;
      updateTitle(null, projectFileName);
      addRecentFile.accept(openProjectPath);
      updateRecentFiles();
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }

  private void resetCCMap() {
    armedMidi = null;
    armedMidiPaint = null;
    CCMap.clear();
    midiButtonMap.keySet().forEach(button -> button.setFill(Color.WHITE));
  }

  public void openProject(String projectFileName) throws Exception {
    if (projectFileName == null) {
      // default project
      // Clone DEFAULT_OBJ InputStream using a ByteArrayOutputStream
      ByteArrayOutputStream baos = new ByteArrayOutputStream();
      DEFAULT_OBJ.transferTo(baos);
      InputStream objClone = new ByteArrayInputStream(baos.toByteArray());

      deleteCurrentFile();
      createFile("cube.obj", objClone.readAllBytes());
      return;
    }
    try {
      DocumentBuilderFactory documentFactory = DocumentBuilderFactory.newInstance();
      documentFactory.setFeature(XMLConstants.FEATURE_SECURE_PROCESSING, true);
      DocumentBuilder documentBuilder = documentFactory.newDocumentBuilder();

      Document document;
      try {
        document = documentBuilder.parse(new File(projectFileName));
      } catch (Exception e) {
        logger.log(Level.WARNING, e.getMessage(), e);
        removeRecentFile.accept(projectFileName);
        openProject(null);
        return;
      }
      document.getDocumentElement().normalize();

      List<BooleanProperty> micSelected = micSelected();
      List<Slider> sliders = sliders();
      List<String> labels = labels();

      // Disable cycling through frames
      generalController.disablePlayback();

      Element root = document.getDocumentElement();
      Element slidersElement = (Element) root.getElementsByTagName("sliders").item(0);
      loadSliderValues(sliders, labels, slidersElement);

      // doesn't exist on newer projects - backwards compatibility
      Element objectRotation = (Element) root.getElementsByTagName("objectRotation").item(0);
      if (objectRotation != null) {
        Element x = (Element) objectRotation.getElementsByTagName("x").item(0);
        Element y = (Element) objectRotation.getElementsByTagName("y").item(0);
        Element z = (Element) objectRotation.getElementsByTagName("z").item(0);
        Slider xSlider = sliders.get(labels.indexOf("objectXRotate"));
        Slider ySlider = sliders.get(labels.indexOf("objectYRotate"));
        Slider zSlider = sliders.get(labels.indexOf("objectZRotate"));
        xSlider.setValue(Double.parseDouble(x.getTextContent()));
        ySlider.setValue(Double.parseDouble(y.getTextContent()));
        zSlider.setValue(Double.parseDouble(z.getTextContent()));
      }

      NodeList nodes = root.getElementsByTagName("micCheckBoxes");
      // backwards compatibility
      if (nodes.getLength() > 0) {
        Element micCheckBoxesElement = (Element) nodes.item(0);
        loadMicCheckBoxes(micSelected, labels, micCheckBoxesElement);
      } else {
        micSelected.forEach(selected -> {
          if (selected != null) {
            selected.setValue(false);
          }
        });
      }

      Element midiElement = (Element) root.getElementsByTagName("midi").item(0);
      resetCCMap();
      for (int i = 0; i < labels.size(); i++) {
        NodeList elements = midiElement.getElementsByTagName(labels.get(i));
        // backwards compatibility
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

      subControllers().forEach(controller -> controller.load(root));

      Element element = (Element) root.getElementsByTagName("checkBoxes").item(0);
      if (element != null) {
        effects().forEach(effect -> effect.load(element));
      } else {
        effects().forEach(effect -> effect.setAnimationType(AnimationType.STATIC));
      }

      Element flipX = (Element) root.getElementsByTagName("flipX").item(0);
      Element flipY = (Element) root.getElementsByTagName("flipY").item(0);
      if (flipX != null) {
        flipXCheckMenuItem.setSelected(Boolean.parseBoolean(flipX.getTextContent()));
      }
      if (flipY != null) {
        flipYCheckMenuItem.setSelected(Boolean.parseBoolean(flipY.getTextContent()));
      }

      Element hiddenEdges = (Element) root.getElementsByTagName("hiddenEdges").item(0);
      if (hiddenEdges != null) {
        hideHiddenMeshesCheckMenuItem.setSelected(Boolean.parseBoolean(hiddenEdges.getTextContent()));
      }

      Element usingGpu = (Element) root.getElementsByTagName("usingGpu").item(0);
      if (usingGpu != null) {
        renderUsingGpuCheckMenuItem.setSelected(Boolean.parseBoolean(usingGpu.getTextContent()));
      }

      Element deadzone = (Element) root.getElementsByTagName("deadzone").item(0);
      if (deadzone != null) {
        deadzoneSpinner.getValueFactory().setValue(Integer.parseInt(deadzone.getTextContent()));
      }

      Element mainMidiChannel = (Element) root.getElementsByTagName("mainMidiChannel").item(0);
      if (mainMidiChannel != null) {
        midiChannelSpinner.getValueFactory().setValue(Integer.parseInt(mainMidiChannel.getTextContent()));
      }

      Element midiAttack = (Element) root.getElementsByTagName("midiAttack").item(0);
      if (midiAttack != null) {
        attackSpinner.getValueFactory().setValue(Double.parseDouble(midiAttack.getTextContent()));
      }

      Element midiDecay = (Element) root.getElementsByTagName("midiDecay").item(0);
      if (midiDecay != null) {
        decaySpinner.getValueFactory().setValue(Double.parseDouble(midiDecay.getTextContent()));
      }

      Element audioSample = (Element) root.getElementsByTagName("audioSample").item(0);
      if (audioSample != null) {
        audioSampleComboBox.setValue(AudioSample.valueOf(audioSample.getTextContent()));
      }

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

      Element frameSource = (Element) root.getElementsByTagName("frameSource").item(0);
      if (frameSource != null) {
        updateFiles(files, fileNames, Integer.parseInt(frameSource.getTextContent()));
      } else {
        updateFiles(files, fileNames, 0);
      }

      Element fontFamily = (Element) root.getElementsByTagName("fontFamily").item(0);
      if (fontFamily != null) {
        fontFamilyListView.getSelectionModel().select(fontFamily.getTextContent());
      }

      Element fontStyle = (Element) root.getElementsByTagName("fontStyle").item(0);
      if (fontStyle != null) {
        fontStyleComboBox.setValue(FontStyle.getStyle(Integer.parseInt(fontStyle.getTextContent())));
      }

      luaController.updateLuaVariables();

      openProjectPath = projectFileName;
      updateTitle(null, projectFileName);
      addRecentFile.accept(openProjectPath);
      updateRecentFiles();
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }

  public void decreaseFrameRate() {
    generalController.decreaseFrameRate();
  }

  public void togglePlayback() {
    if (frameSources.size() != 1) {
      generalController.togglePlayback();
    }
  }

  public void increaseFrameRate() {
    generalController.increaseFrameRate();
  }

  @Override
  public void transmit(double sample) {
    micSamples[micSampleIndex++] = sample;
    if (micSampleIndex >= micSamples.length) {
      micSampleIndex = 0;
    }
    double volume = 0;
    for (double micSample : micSamples) {
      volume += Math.abs(micSample);
    }
    volume /= micSamples.length;
    volume = Math.min(1.0, Math.max(-1.0, volume * generalController.getMicVolume()));
    if (volume < MIN_MIC_VOLUME && volume > -MIN_MIC_VOLUME) {
      volume = 0;
    }

    List<Slider> sliders = sliders();
    List<BooleanProperty> micSelected = micSelected();

    double finalVolume = volume;
    Platform.runLater(() -> {
      for (int i = 0; i < micSelected.size(); i++) {
        // allow for sliders without a mic checkbox
        if (micSelected.get(i) != null) {
          if (micSelected.get(i).get()) {
            Slider slider = sliders.get(i);
            double sliderValue = targetSliderValue[i] + (slider.getMax() - slider.getMin()) * finalVolume;
            if (sliderValue > slider.getMax()) {
              sliderValue = slider.getMax();
            } else if (sliderValue < slider.getMin()) {
              sliderValue = slider.getMin();
            }
            sliders.get(i).setValue(sliderValue);
          }
        }
      }
    });
  }

  void openCodeEditor() {
    String code = new String(openFiles.get(currentFrameSource), StandardCharsets.UTF_8);
    closeCodeEditor();
    String name = frameSourcePaths.get(currentFrameSource);
    String mimeType = "";
    if (LuaParser.isLuaFile(name)) {
      mimeType = "text/x-lua";
    } else if (SvgParser.isSvgFile(name)) {
      mimeType = "text/html";
    }
    launchCodeEditor(code, frameSourcePaths.get(currentFrameSource), mimeType, (data, fileName) -> updateFileData(data, fileName, true));
  }

  private void updateTitle(String message, String projectName) {
    String title = "osci-render";
    if (projectName != null) {
      title += " - " + projectName;
    }
    if (message != null) {
      title += " - " + message;
    }
    stage.setTitle(title);
  }

  public void resetLuaStep() {
    sampleParsers.forEach(sampleParser -> {
      if (sampleParser instanceof LuaParser luaParser) {
        luaParser.resetStep();
      }
    });
  }

  public void setAddRecentFile(Consumer<String> addRecentFile) {
    this.addRecentFile = addRecentFile;
  }

  public void setRemoveRecentFile(Consumer<String> removeRecentFile) {
    this.removeRecentFile = removeRecentFile;
  }

  public void setVolume(double volume) {
    volumeController.setVolume(volume);
  }

  public void initialiseAudioEngine() throws Exception {
    FrameSource<List<Shape>> frames = new ShapeFrameSource(List.of(new Vector2()));
    openFiles.add(new byte[0]);
    frameSources.add(frames);
    sampleParsers.add(null);
    frameSourcePaths.add("Empty file");
    currentFrameSource = 0;
    producer = new FrameProducer<>(audioPlayer, frames);
    objController.setAudioProducer(producer);

    if (defaultDevice == null) {
      throw new RuntimeException("No default audio device found!");
    }
    sampleRate = defaultDevice.sampleRate();

    objController.updateObjectRotateSpeed();

    executor.submit(producer);
    Gui.midiCommunicator.addListener(this);
    AudioInput audioInput = new JavaAudioInput();
    if (audioInput.isAvailable()) {
      audioInput.addListener(this);
      new Thread(audioInput).start();
    } else {
      subControllers().forEach(SubController::micNotAvailable);
      micSelected().forEach(prop -> {
        if (prop != null) {
          prop.setValue(false);
        }
      });
    }

    updateRecentFiles();

    luaController.updateLuaVariables();

    objectServer = new ObjectServer(this::enableObjectServerRendering, this::disableObjectServerRendering);
    new Thread(objectServer).start();

    webSocketServer = new ByteWebSocketServer();
    webSocketServer.setReuseAddr(true);
    webSocketServer.start();
    this.buffer = new byte[FRAME_SIZE * SOSCI_NUM_VERTICES * SOSCI_VERTEX_SIZE];
    new Thread(() -> sendAudioDataToWebSocket(webSocketServer)).start();
  }

  public void setRecentFiles(Callable<List<String>> recentFiles) {
    this.recentFiles = recentFiles;
  }

  private void updateRecentFiles() throws Exception {
    recentProjectMenu.getItems().clear();
    recentProjectMenu.getItems().addAll(
      recentFiles
        .call()
        .stream()
        .limit(MAX_RECENT_FILES)
        .map(MenuItem::new)
        .toList()
    );
    recentProjectMenu.getItems().forEach(item -> {
      item.setOnAction(e -> {
        try {
          openProject(item.getText());
        } catch (Exception ex) {
          logger.log(Level.SEVERE, ex.getMessage(), ex);
        }
      });
    });
  }

  private record PrintableSlider(Slider slider) {
    @Override
    public String toString() {
      // from https://stackoverflow.com/a/2560017/9707097
      String humanReadable = slider.getId()
        .replaceFirst("Slider$", "")
        .replaceAll(
          String.format("%s|%s|%s",
            "(?<=[A-Z])(?=[A-Z][a-z])",
            "(?<=[^A-Z])(?=[A-Z])",
            "(?<=[A-Za-z])(?=[^A-Za-z])"
          ),
          " "
      );
      return humanReadable.substring(0, 1).toUpperCase() + humanReadable.substring(1);
    }
  }
}
