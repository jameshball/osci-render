package sh.ball.gui.controller;

import javafx.application.Platform;
import javafx.scene.control.*;
import javafx.scene.paint.Color;
import javafx.scene.paint.Paint;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;
import sh.ball.audio.*;

import java.io.*;
import java.net.URL;
import java.util.*;
import java.util.concurrent.*;

import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.stage.FileChooser;
import javafx.stage.Stage;

import javax.sound.midi.ShortMessage;
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
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.midi.MidiListener;
import sh.ball.audio.midi.MidiNote;
import sh.ball.engine.Vector3;
import sh.ball.gui.Gui;
import sh.ball.parser.obj.ObjFrameSettings;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.ParserFactory;
import sh.ball.shapes.Shape;

import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.gui.Gui.defaultDevice;

public class MainController implements Initializable, FrequencyListener, MidiListener {

  private String openProjectPath;

  // audio
  private int sampleRate;
  private FrequencyAnalyser<List<Shape>> analyser;

  // midi
  private final Map<Integer, SVGPath> CCMap = new HashMap<>();
  private Paint armedMidiPaint;
  private SVGPath armedMidi;
  private Map<SVGPath, Slider> midiButtonMap;

  // frames
  private static final InputStream DEFAULT_OBJ = MainController.class.getResourceAsStream("/models/cube.obj");
  private final ExecutorService executor = Executors.newSingleThreadExecutor();
  private List<byte[]> openFiles = new ArrayList<>();
  private List<String> frameSourcePaths = new ArrayList<>();
  private List<FrameSource<List<Shape>>> frameSources = new ArrayList<>();
  private FrameProducer<List<Shape>> producer;
  private int currentFrameSource;

  // javafx
  private final FileChooser osciFileChooser = new FileChooser();
  private Stage stage;

  @FXML
  private EffectsController effectsController;
  @FXML
  private ObjController objController;
  @FXML
  private ImageController imageController;
  @FXML
  private GeneralController generalController;
  @FXML
  private TitledPane objTitledPane;
  @FXML
  private MenuItem openProjectMenuItem;
  @FXML
  private MenuItem saveProjectMenuItem;
  @FXML
  private MenuItem saveAsProjectMenuItem;

  public MainController() throws Exception {
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
    if (defaultDevice == null) {
      throw new RuntimeException("No default audio device found!");
    }
    this.sampleRate = defaultDevice.sampleRate();
  }

  // initialises midiButtonMap by mapping MIDI logo SVGs to the slider that they
  // control if they are selected.
  private Map<SVGPath, Slider> initializeMidiButtonMap() {
    Map<SVGPath, Slider> midiMap = new HashMap<>();
    midiMap.putAll(generalController.getMidiButtonMap());
    midiMap.putAll(imageController.getMidiButtonMap());
    midiMap.putAll(objController.getMidiButtonMap());
    midiMap.putAll(effectsController.getMidiButtonMap());
    return midiMap;
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    objController.setAudioProducer(producer);
    generalController.setMainController(this);

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
        openProject(file.getAbsolutePath());
      }
    });

    objController.updateObjectRotateSpeed();

    switchAudioDevice(defaultDevice, false);
    executor.submit(producer);
    Gui.midiCommunicator.addListener(this);
  }

  // used when a file is chosen so that the same folder is reopened when a
  // file chooser opens
  private void updateLastVisitedDirectory(File file) {
    String lastVisitedDirectory = file != null ? file.getAbsolutePath() : System.getProperty("user.home");
    File dir = new File(lastVisitedDirectory);
    osciFileChooser.setInitialDirectory(dir);
    generalController.updateLastVisitedDirectory(dir);
  }

  void switchAudioDevice(AudioDevice device) {
    switchAudioDevice(device, true);
  }

  // restarts audioPlayer and FrequencyAnalyser to support new device
  private void switchAudioDevice(AudioDevice device, boolean reset) {
    if (reset) {
      try {
        audioPlayer.reset();
      } catch (Exception e) {
        e.printStackTrace();
      }
    }
    audioPlayer.setDevice(device);
    effectsController.setAudioDevice(device);
    imageController.setAudioDevice(device);
    sampleRate = device.sampleRate();
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
    audioPlayerThread.start();
  }

  // creates a new thread for the frequency analyser and adds the wobble effect and the controller
  // as listeners of it so that they can get updates as the frequency changes
  private void startFrequencyAnalyser(FrequencyAnalyser<List<Shape>> analyser) {
    analyser.addListener(this);
    effectsController.setFrequencyAnalyser(analyser);
    new Thread(analyser).start();
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

    Object oldSettings = producer.getFrameSettings();
    producer = new FrameProducer<>(audioPlayer, frames);
    objController.setAudioProducer(producer);

    // Apply the same settings that the previous frameSource had
    objController.updateObjectRotateSpeed();
    objController.updateFocalLength();
    if (oldSettings instanceof ObjFrameSettings settings) {
      setObjRotate(settings.baseRotation, settings.currentRotation);
    }
    executor.submit(producer);
    effectsController.restartEffects();

    generalController.setFrameSourceName(frameSourcePaths.get(currentFrameSource));
    generalController.updateFrameLabels();
    // enable the .obj file settings iff the new frameSource is for a 3D object.
    objTitledPane.setDisable(!ObjParser.isObjFile(frameSourcePaths.get(index)));
  }

  void updateFiles(List<byte[]> files, List<String> names) throws IOException, ParserConfigurationException, SAXException {
    List<FrameSource<List<Shape>>> newFrameSources = new ArrayList<>();
    List<String> newFrameSourcePaths = new ArrayList<>();
    List<byte[]> newOpenFiles = new ArrayList<>();

    for (int i = 0; i < files.size(); i++) {
      try {
        newFrameSources.add(ParserFactory.getParser(names.get(i), files.get(i)).parse());
        newFrameSourcePaths.add(names.get(i));
        newOpenFiles.add(files.get(i));
      } catch (IOException ignored) {}
    }

    if (newFrameSources.size() > 0) {
      generalController.showMultiFileTooltip(newFrameSources.size() > 1);
      if (generalController.framesPlaying() && newFrameSources.size() == 1) {
        generalController.disablePlayback();
      }
      frameSources.forEach(FrameSource::disable);
      frameSources = newFrameSources;
      frameSourcePaths = newFrameSourcePaths;
      openFiles = newOpenFiles;
      changeFrameSource(0);
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
  public boolean mouseRotate() {
    return objController.mouseRotate();
  }

  // stops the mouse rotating the 3D object when ESC is pressed or checkbox is
  // unchecked
  public void disableMouseRotate() {
    objController.disableMouseRotate();
  }

  // updates the 3D object base rotation angle
  public void setObjRotate(Vector3 vector) {
    objController.setObjRotate(vector);
  }

  // updates the 3D object base and current rotation angle
  protected void setObjRotate(Vector3 baseRotation, Vector3 currentRotation) {
    objController.setObjRotate(baseRotation, currentRotation);
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
  private List<Slider> otherSliders() {
    List<Slider> sliders = new ArrayList<>();
    sliders.addAll(generalController.sliders());
    sliders.addAll(imageController.sliders());
    sliders.addAll(objController.sliders());
    return sliders;
  }
  private List<String> otherLabels() {
    List<String> labels = new ArrayList<>();
    labels.addAll(generalController.labels());
    labels.addAll(imageController.labels());
    labels.addAll(objController.labels());
    return labels;
  }
  private List<Slider> allSliders() {
    List<Slider> sliders = new ArrayList<>(effectsController.sliders());
    sliders.addAll(otherSliders());
    return sliders;
  }
  private List<String> allLabels() {
    List<String> labels = new ArrayList<>(effectsController.labels());
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

      List<Slider> sliders = allSliders();
      List<String> labels = allLabels();

      Element slidersElement = document.createElement("sliders");
      appendSliders(sliders, labels, slidersElement, document);
      root.appendChild(slidersElement);

      root.appendChild(effectsController.save(document));

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

      root.appendChild(imageController.save(document));

      root.appendChild(objController.save(document));

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

      List<Slider> sliders = allSliders();
      List<String> labels = allLabels();

      // Disable cycling through frames
      generalController.disablePlayback();

      Element root = document.getDocumentElement();
      Element slidersElement = (Element) root.getElementsByTagName("sliders").item(0);
      loadSliderValues(sliders, labels, slidersElement);

      effectsController.load(root);

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

      imageController.load(root);

      objController.load(root);

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
}
