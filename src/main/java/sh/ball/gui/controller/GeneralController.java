package sh.ball.gui.controller;

import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.collections.FXCollections;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.input.KeyCode;
import javafx.scene.paint.Color;
import javafx.scene.shape.SVGPath;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import javafx.stage.Stage;
import javafx.util.Duration;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.gui.components.EffectComponentGroup;

import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.*;

public class GeneralController implements Initializable, SubController {

  private MainController mainController;

  private final FileChooser renderFileChooser = new FileChooser();
  private final DirectoryChooser folderChooser = new DirectoryChooser();
  private Stage stage;
  private boolean lineInSelected = false;

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
  private Button editFileButton;
  @FXML
  private Button chooseFolderButton;
  @FXML
  private Label fileLabel;
  @FXML
  private Label jkLabel;
  @FXML
  private Label recordLabel;
  @FXML
  private Slider octaveSlider;
  @FXML
  private SVGPath octaveMidi;
  @FXML
  private Slider micVolumeSlider;
  @FXML
  private SVGPath micVolumeMidi;
  @FXML
  private TextField createFileTextField;
  @FXML
  private ComboBox<String> createFileComboBox;
  @FXML
  private Button createFileButton;
  @FXML
  private Button deleteFileButton;
  @FXML
  private SVGPath lineInToggle;

  public void setRecordResult(String result) {
    recordLabel.setText(result);
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

        new Thread(() -> {
          try {
            mainController.updateFiles(files, names, 0);
          } catch (Exception e) {
            logger.log(Level.SEVERE, e.getMessage(), e);
          }
        }).start();
      }
    } catch (IOException ioException) {
      logger.log(Level.SEVERE, ioException.getMessage(), ioException);
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

    editFileButton.setOnAction(e -> mainController.openCodeEditor());
    deleteFileButton.setOnAction(e -> mainController.deleteCurrentFile());

    createFileComboBox.setItems(FXCollections.observableList(List.of(".lua", ".svg", ".obj", ".txt")));
    createFileComboBox.setValue(".lua");

    try {
      Map<String, byte[]> defaultFileMap = Map.of(
        ".lua", getClass().getResourceAsStream("/lua/demo.lua").readAllBytes(),
        ".svg", getClass().getResourceAsStream("/svg/demo.svg").readAllBytes(),
        ".obj", getClass().getResourceAsStream("/models/cube.obj").readAllBytes(),
        ".txt", "hello".getBytes(StandardCharsets.UTF_8)
      );

      createFileButton.setOnAction(e -> {
        try {
          String fileName = createFileTextField.getText() + createFileComboBox.getValue();
          byte[] fileData = defaultFileMap.get(createFileComboBox.getValue());
          mainController.createFile(fileName, fileData);
          createFileTextField.setText("");
        } catch (Exception ex) {
          logger.log(Level.SEVERE, ex.getMessage(), ex);
        }
      });
    } catch (IOException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }

    createFileTextField.setOnKeyPressed(event -> {
      if (event.getCode() == KeyCode.ENTER) {
        createFileButton.getOnAction().handle(null);
      }
    });

    lineInToggle.setOnMouseClicked(e -> {
      lineInSelected = !lineInSelected;
      audioPlayer.toggleLineIn();
      lineInToggle.setFill(lineInSelected ? Color.RED : Color.WHITE);
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
    renderFileChooser.setInitialDirectory(dir);
    folderChooser.setInitialDirectory(dir);
  }

  public void updateFrequency(double leftFrequency, double rightFrequency) {
    Platform.runLater(() ->
      frequencyLabel.setText(String.format("L/R Frequency: %5d Hz\t / %5d Hz", Math.round(leftFrequency), Math.round(rightFrequency)))
    );
  }

  public double getMicVolume() {
    return micVolumeSlider.getValue();
  }

  @Override
  public List<BooleanProperty> micSelected() {
    List<BooleanProperty> checkboxes = new ArrayList<>();
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
  public List<EffectComponentGroup> effects() {
    return List.of();
  }

  @Override
  public List<Element> save(Document document) {
    Element element = document.createElement("general");
    Element frameRate = document.createElement("frameRate");
    frameRate.appendChild(document.createTextNode(Integer.toString(this.frameRate)));
    element.appendChild(frameRate);
    return List.of(element);
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

  @Override
  public void micNotAvailable() {
    lineInToggle.setDisable(true);
    lineInToggle.setContent("M19 11c0 1.19-.34 2.3-.9 3.28l-1.23-1.23c.27-.62.43-1.31.43-2.05H19m-4 .16L9 5.18V5a3 3 0 0 1 3-3a3 3 0 0 1 3 3v6.16M4.27 3L21 19.73L19.73 21l-4.19-4.19c-.77.46-1.63.77-2.54.91V21h-2v-3.28c-3.28-.49-6-3.31-6-6.72h1.7c0 3 2.54 5.1 5.3 5.1c.81 0 1.6-.19 2.31-.52l-1.66-1.66L12 14a3 3 0 0 1-3-3v-.72L3 4.27L4.27 3Z");
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
