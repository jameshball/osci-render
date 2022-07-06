package sh.ball.gui;

import com.sun.javafx.PlatformUtil;
import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.SceneAntialiasing;
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
import sh.ball.gui.controller.ProjectSelectController;
import sh.ball.parser.lua.LuaParser;
import sh.ball.parser.obj.ObjParser;
import sh.ball.parser.svg.SvgParser;
import sh.ball.parser.txt.TextParser;
import sh.ball.shapes.Vector2;

import java.io.File;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Objects;
import java.util.logging.*;

public class Gui extends Application {

  // These need to be global so that we can guarantee they can be accessed by Controllers
  public static final MidiCommunicator midiCommunicator = new MidiCommunicator();
  public static String LOG_DIR = "./logs/";
  public static final Logger logger = Logger.getLogger(Gui.class.getName());

  public static ProjectSelectController projectSelectController;
  public static ShapeAudioPlayer audioPlayer;
  public static AudioDevice defaultDevice;
  public static CodeEditor editor;
  public static Stage editorStage;
  public static Scene editorScene;

  static {
    try {
      audioPlayer = new ShapeAudioPlayer(ConglomerateAudioEngine::new, midiCommunicator);
      defaultDevice = audioPlayer.getDefaultDevice();

      if (PlatformUtil.isWindows()) {
        LOG_DIR = System.getenv("AppData");
      } else if (PlatformUtil.isUnix() || PlatformUtil.isMac()) {
        LOG_DIR = System.getProperty("user.home");
      } else {
        throw new RuntimeException("OS not recognised");
      }

      LOG_DIR += "/osci-render/logs/";

      File directory = new File(LOG_DIR);
      if (!directory.exists()){
        directory.mkdirs();
      }
      Handler fileHandler = new FileHandler(LOG_DIR + new SimpleDateFormat("yyyy.MM.dd.HH.mm.ss").format(new Date()) + ".log");
      fileHandler.setLevel(Level.WARNING);
      fileHandler.setFormatter(new SimpleFormatter());

      Logger javafxLogger = Logger.getLogger("javafx");
      logger.addHandler(fileHandler);
      javafxLogger.addHandler(fileHandler);
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
    new Thread(midiCommunicator).start();
  }

  private Stage stage;
  private Scene scene;
  private Parent root;

  @Override
  public void start(Stage stage) throws Exception {
    this.stage = stage;
    System.setProperty("prism.lcdtext", "false");
    System.setProperty("org.luaj.luajc", "true");

    Thread.setDefaultUncaughtExceptionHandler((t, e) -> logger.log(Level.SEVERE, e.getMessage(), e));

    editor = new CodeEditor();
    editor.initialize();
    editorStage = new Stage();
    editorScene = new Scene(editor, 900, 600, false, SceneAntialiasing.BALANCED);
    editorStage.setScene(editorScene);
    editorStage.getIcons().add(new Image(Objects.requireNonNull(Gui.class.getResourceAsStream("/icons/icon.png"))));
    editor.prefHeightProperty().bind(editorStage.heightProperty());
    editor.prefWidthProperty().bind(editorStage.widthProperty());

    FXMLLoader projectSelectLoader = new FXMLLoader(getClass().getResource("/fxml/projectSelect.fxml"));
    Parent projectSelectRoot = projectSelectLoader.load();
    projectSelectController = projectSelectLoader.getController();
    projectSelectController.setApplicationLauncher(this::launchMainApplication);
    projectSelectController.setOpenBrowser(url -> getHostServices().showDocument(url));

    stage.getIcons().add(new Image(Objects.requireNonNull(getClass().getResourceAsStream("/icons/icon.png"))));
    stage.setTitle("osci-render");
    scene = new Scene(projectSelectRoot);
    scene.getStylesheets().add(getClass().getResource("/css/main.css").toExternalForm());

    stage.setScene(scene);
    stage.setResizable(false);

    stage.setOnCloseRequest(t -> {
      Platform.exit();
      System.exit(0);
    });

    stage.show();

    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main.fxml"));
    root = loader.load();
    MainController controller = loader.getController();

    controller.setAddRecentFile(projectSelectController::addRecentFile);

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

    controller.setStage(stage);

    stage.setOnCloseRequest(t -> {
      controller.shutdown();
      Platform.exit();
      System.exit(0);
    });
  }

  public void launchMainApplication() {
    scene.setRoot(root);
  }

  public static void launchCodeEditor(String code, String fileName) {
    editor.setCode(code, fileName);
    editorStage.show();
    editorStage.setTitle(fileName);

    if (LuaParser.isLuaFile(fileName)) {
      editor.setMode("text/x-lua");
    } else if (ObjParser.isObjFile(fileName)) {
      editor.setMode("");
    } else if (SvgParser.isSvgFile(fileName)) {
      editor.setMode("text/html");
    } else if (TextParser.isTxtFile(fileName)) {
      editor.setMode("");
    }
  }

  public static void closeCodeEditor() {
    if (editorStage != null) {
      editorStage.close();
    }
  }

  public static void main(String[] args) {
    try {
      launch(args);
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }
}
