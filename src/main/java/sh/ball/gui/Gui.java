package sh.ball.gui;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.scene.input.KeyCode;
import javafx.scene.input.KeyEvent;
import javafx.scene.input.MouseEvent;
import javafx.stage.Stage;
import sh.ball.audio.ShapeAudioPlayer;
import sh.ball.audio.engine.AudioDevice;
import sh.ball.audio.engine.ConglomerateAudioEngine;
import sh.ball.audio.midi.MidiCommunicator;
import sh.ball.gui.components.CodeEditor;
import sh.ball.gui.controller.MainController;
import sh.ball.shapes.Vector2;

import java.util.Objects;

public class Gui extends Application {

  // These need to be global so that we can guarantee they can be accessed by Controllers
  public static final MidiCommunicator midiCommunicator = new MidiCommunicator();
  public static ShapeAudioPlayer audioPlayer;
  public static AudioDevice defaultDevice;
  public static CodeEditor editor;
  public static Stage editorStage;
  public static Scene editorScene;

  static {
    try {
      audioPlayer = new ShapeAudioPlayer(ConglomerateAudioEngine::new, midiCommunicator);
      defaultDevice = audioPlayer.getDefaultDevice();
    } catch (Exception e) {
      e.printStackTrace();
    }
    new Thread(midiCommunicator).start();
  }

  @Override
  public void start(Stage stage) throws Exception {
    System.setProperty("prism.lcdtext", "false");

    editor = new CodeEditor();
    editor.initialize();
    editorStage = new Stage();
    editorScene = new Scene(editor);
    editorStage.setScene(editorScene);
    editorStage.getIcons().add(new Image(Objects.requireNonNull(Gui.class.getResourceAsStream("/icons/icon.png"))));
    editorStage.setWidth(900);
    editorStage.setHeight(600);
    editor.prefHeightProperty().bind(editorStage.heightProperty());
    editor.prefWidthProperty().bind(editorStage.widthProperty());

    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main.fxml"));
    Parent root = loader.load();
    MainController controller = loader.getController();

    stage.getIcons().add(new Image(Objects.requireNonNull(getClass().getResourceAsStream("/icons/icon.png"))));
    stage.setTitle("osci-render");
    Scene scene = new Scene(root);
    scene.getStylesheets().add(getClass().getResource("/css/main.css").toExternalForm());

    controller.setSoftwareOscilloscopeAction(() -> {
      getHostServices().showDocument("https://james.ball.sh/oscilloscope");
    });

    scene.addEventHandler(KeyEvent.KEY_PRESSED, (event -> {
      switch (event.getCode()) {
        // frame selection controls
        case J -> controller.nextFrameSource();
        case K -> controller.previousFrameSource();
        // playback controls
        case U -> controller.decreaseFrameRate();
        case I -> controller.togglePlayback();
        case O -> controller.increaseFrameRate();
      }
    }));

    scene.addEventFilter(MouseEvent.MOUSE_MOVED, event -> {
      if (controller.mouseRotate()) {
        controller.setMouseXY(event.getSceneX() / scene.getWidth(), event.getSceneY() / scene.getHeight());
      }
      if (controller.mouseTranslate()) {
        controller.setTranslation(new Vector2(
          2 * event.getSceneX() / scene.getWidth() - 1,
          1 - 2 * event.getSceneY() / scene.getHeight()
        ));
      }
    });

    scene.addEventHandler(KeyEvent.KEY_PRESSED, t -> {
      if (t.getCode() == KeyCode.ESCAPE) {
        controller.disableMouseRotate();
        controller.disableMouseTranslate();
      }
    });

    stage.setScene(scene);
    stage.setResizable(false);

    controller.setStage(stage);

    stage.show();

    stage.setOnCloseRequest(t -> {
      controller.shutdown();
      Platform.exit();
      System.exit(0);
    });
  }

  public static void launchCodeEditor(String code, String fileName) {
    editor.setCode(code, fileName);
    editorStage.show();
    editorStage.setTitle(fileName);
  }

  public static void closeCodeEditor() {
    if (editorStage != null) {
      editorStage.close();
    }
  }

  public static void main(String[] args) {
    launch(args);
  }
}
