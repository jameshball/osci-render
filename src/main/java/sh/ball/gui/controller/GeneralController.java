package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.shape.SVGPath;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import javafx.stage.Stage;
import javafx.util.Duration;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.xml.sax.SAXException;
import sh.ball.audio.engine.AudioDevice;

import javax.sound.sampled.AudioFileFormat;
import javax.sound.sampled.AudioInputStream;
import javax.sound.sampled.AudioSystem;
import javax.xml.parsers.ParserConfigurationException;
import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.file.Files;
import java.text.SimpleDateFormat;
import java.util.*;

import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.gui.Gui.defaultDevice;

public class GeneralController implements Initializable, SubController {

  private MainController mainController;

  private final FileChooser wavFileChooser = new FileChooser();
  private final FileChooser renderFileChooser = new FileChooser();
  private final DirectoryChooser folderChooser = new DirectoryChooser();
  private Stage stage;

  private boolean recording = false;
  private Timeline recordingTimeline;

  // frame playback (code by DJ_Level_3)
  private static final int MAX_FRAME_RATE = 120;
  private static final int MIN_FRAME_RATE = 1;
  private boolean framesPlaying = false; // default to not playing
  private int frameRate = 10; // default to 10 frames per second
  private String frameSourceName;

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
  private Slider octaveSlider;
  @FXML
  private SVGPath octaveMidi;
  @FXML
  private Slider micVolumeSlider;
  @FXML
  private SVGPath micVolumeMidi;
  @FXML
  private ComboBox<AudioDevice> deviceComboBox;

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

  // selects a new file or folder for files to be rendered from
  private void chooseFile(File chosenFile) {
    try {
      if (chosenFile.exists()) {
        List<byte[]> files = new ArrayList<>();
        List<String> names = new ArrayList<>();
        if (chosenFile.isDirectory()) {
          File[] fileList = Objects.requireNonNull(chosenFile.listFiles());
          Arrays.sort(fileList);
          for (File file : fileList) {
            files.add(Files.readAllBytes(file.toPath()));
            names.add(file.getName());
          }
        } else {
          files.add(Files.readAllBytes(chosenFile.toPath()));
          names.add(chosenFile.getName());
        }

        mainController.updateFiles(files, names);
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

  public void setMainController(MainController mainController) {
    this.mainController = mainController;
  }

  // ==================== Start code block by DJ_Level_3 ====================
  //
  //     Quickly written code by DJ_Level_3, a programmer who is not super
  // experienced with Java. It almost definitely could be made better in one
  // way or another, but testing so far shows that the code is stable and
  // doesn't noticeably decrease performance.
  //

  public void updateFrameLabels() {
    if (framesPlaying) {
      fileLabel.setText("Frame rate: " + frameRate);
      jkLabel.setText("Use u and o to decrease and increase the frame rate, or i to stop playback");
    } else {
      fileLabel.setText(frameSourceName);
      jkLabel.setText("Use j and k (or MIDI Program Change) to cycle between files, or i to start playback");
    }
  }

  public boolean framesPlaying() {
    return framesPlaying;
  }

  public void disablePlayback() {
    framesPlaying = false;
  }

  private void enablePlayback() {
    framesPlaying = true;
    doPlayback();
  }

  // toggles frameSource playback after pressing 'i'
  public void togglePlayback() {
    if (framesPlaying) {
      disablePlayback();
    } else {
      enablePlayback();
    }
    updateFrameLabels();
  }

  // increments frameRate (up to maximum) after pressing 'u'
  public void increaseFrameRate() {
    if (frameRate < MAX_FRAME_RATE) {
      frameRate++;
    } else {
      frameRate = MAX_FRAME_RATE;
    }
    updateFrameLabels();
  }

  // decrements frameRate (down to minimum) after pressing 'o'
  public void decreaseFrameRate() {
    if (frameRate > MIN_FRAME_RATE) {
      frameRate--;
    } else {
      frameRate = MIN_FRAME_RATE;
    }
    updateFrameLabels();
  }

  // repeatedly swaps frameSource when playback is enabled
  private void doPlayback() {
    if (framesPlaying) {
      KeyFrame now = new KeyFrame(Duration.seconds(0), e -> mainController.nextFrameSource());
      KeyFrame next = new KeyFrame(Duration.seconds(1.0 / frameRate), e -> {
        doPlayback();
      });
      Timeline timeline = new Timeline(now, next);
      Platform.runLater(timeline::play);
    } else {
      mainController.nextFrameSource();
    }
  }

  // ====================  End code block by DJ_Level_3  ====================

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    octaveSlider.valueProperty().addListener((e, old, octave) -> audioPlayer.setOctave(octave.intValue()));

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

    List<AudioDevice> devices = audioPlayer.devices();
    deviceComboBox.setItems(FXCollections.observableList(devices));
    deviceComboBox.setValue(defaultDevice);
    deviceComboBox.valueProperty().addListener((options, oldDevice, newDevice) -> {
      if (newDevice != null) {
        mainController.switchAudioDevice(newDevice);
      }
    });
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      octaveMidi, octaveSlider,
      micVolumeMidi, micVolumeSlider
    );
  }

  public void updateLastVisitedDirectory(File dir) {
    wavFileChooser.setInitialDirectory(dir);
    renderFileChooser.setInitialDirectory(dir);
    folderChooser.setInitialDirectory(dir);
  }

  public void updateFrequency(double leftFrequency, double rightFrequency) {
    Platform.runLater(() ->
      frequencyLabel.setText(String.format("L/R Frequency:\n%d Hz / %d Hz", Math.round(leftFrequency), Math.round(rightFrequency)))
    );
  }

  public double getMicVolume() {
    return micVolumeSlider.getValue();
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    List<CheckBox> checkboxes = new ArrayList<>();
    checkboxes.add(null);
    checkboxes.add(null);
    return checkboxes;
  }
  @Override
  public List<Slider> sliders() {
    return List.of(octaveSlider, micVolumeSlider);
  }

  @Override
  public List<String> labels() {
    return List.of("octave", "micVolume");
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("general");
    Element frameRate = document.createElement("frameRate");
    frameRate.appendChild(document.createTextNode(Integer.toString(this.frameRate)));
    element.appendChild(frameRate);
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("general").item(0);
    if (element != null) {
      Element frameRate = (Element) element.getElementsByTagName("frameRate").item(0);
      if (frameRate != null) {
        this.frameRate = Integer.parseInt(frameRate.getTextContent());
      }
    }
  }

  public void setFrameSourceName(String name) {
    this.frameSourceName = name;
  }

  public void showMultiFileTooltip(boolean visible) {
    jkLabel.setVisible(visible);
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }
}
