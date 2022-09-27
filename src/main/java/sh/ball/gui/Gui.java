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
import java.nio.file.Path;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Objects;
import java.util.function.BiConsumer;
import java.util.logging.*;

public class Gui extends Application {

  // These need to be global so that we can guarantee they can be accessed by Controllers
  public static final MidiCommunicator midiCommunicator = new MidiCommunicator();
  public static String LOG_DIR = "./logs/";
  public static final Logger logger = Logger.getLogger(Gui.class.getName());

  public static ShapeAudioPlayer audioPlayer;
  public static AudioDevice defaultDevice;

  private static CodeEditor editor;
  private static Stage editorStage;

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
      Handler fileHandler = new FileHandler(LOG_DIR + new SimpleDateFormat("yyyy.MM.dd.HH.mm.ss").format(new Date()) + ".log", 1024 * 1024, 1);
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

  private Scene scene;
  private Parent root;

  @Override
  public void start(Stage stage) throws Exception {
    System.setProperty("prism.lcdtext", "false");
    System.setProperty("org.luaj.luajc", "true");

    Thread.setDefaultUncaughtExceptionHandler((t, e) -> logger.log(Level.SEVERE, e.getMessage(), e));

    FXMLLoader projectSelectLoader = new FXMLLoader(getClass().getResource("/fxml/projectSelect.fxml"));
    Parent projectSelectRoot = projectSelectLoader.load();
    ProjectSelectController projectSelectController = projectSelectLoader.getController();
    projectSelectController.setOpenBrowser(url -> getHostServices().showDocument(url));

    stage.getIcons().add(new Image(Objects.requireNonNull(getClass().getResourceAsStream("/icons/icon.png"))));
    stage.setTitle("osci-render");
    scene = new Scene(projectSelectRoot);
    scene.getStylesheets().add(getClass().getResource("/css/main.css").toExternalForm());

    stage.setScene(scene);
    stage.setResizable(true);

    stage.setOnCloseRequest(t -> {
      Platform.exit();
      System.exit(0);
    });

    stage.show();

    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/main.fxml"));
    root = loader.load();
    MainController controller = loader.getController();

    projectSelectController.setApplicationLauncher((path, muted) -> launchMainApplication(controller, path, muted));

    controller.setAddRecentFile(projectSelectController::addRecentFile);
    controller.setRemoveRecentFile(projectSelectController::removeRecentFile);
    controller.setRecentFiles(projectSelectController::recentFiles);

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

    editor = new CodeEditor();
    editor.initialize();
    editorStage = new Stage();
    Scene editorScene = new Scene(editor, 900, 600, false, SceneAntialiasing.BALANCED);
    editorStage.setScene(editorScene);
    editorStage.getIcons().add(new Image(Objects.requireNonNull(Gui.class.getResourceAsStream("/icons/icon.png"))));
    editor.prefHeightProperty().bind(editorStage.heightProperty());
    editor.prefWidthProperty().bind(editorStage.widthProperty());
  }

  public void launchMainApplication(MainController controller, String projectPath, Boolean muted) throws Exception {
    scene.setRoot(root);
    controller.initialiseAudioEngine();
    controller.openProject(projectPath);
    if (muted) {
      controller.setVolume(0);
    }
  }

  public static void openFileExplorer(String directory) throws IOException {
    String path = Path.of(directory).toAbsolutePath().toString();
    if (PlatformUtil.isWindows()) {
      Runtime.getRuntime().exec("explorer /root, " + path);
    } else if (PlatformUtil.isMac()) {
      Runtime.getRuntime().exec("open " + path);
    } else {
      Runtime.getRuntime().exec("xdg-open " + path);
    }
  }

  public static void launchCodeEditor(String code, String fileName, String mimeType, BiConsumer<byte[], String> callback) {
    editor.setCode(code, fileName);
    editor.setCallback(callback);
    editorStage.show();
    editorStage.setTitle(fileName);
    // hacky way to bring the window to the front
    editorStage.setAlwaysOnTop(true);
    editorStage.setAlwaysOnTop(false);
    editor.setMode(mimeType);
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
