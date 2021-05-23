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
import sh.ball.audio.AudioPlayer;
import sh.ball.engine.Vector3;

import java.util.Objects;

public class Gui extends Application {

  private static final int SAMPLE_RATE = 192000;

  @Override
  public void start(Stage stage) throws Exception {
    Thread.currentThread().setPriority(Thread.MAX_PRIORITY);

    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/osci-render.fxml"));
    Controller controller = new Controller(new AudioPlayer(SAMPLE_RATE));
    loader.setController(controller);
    Parent root = loader.load();

    stage.getIcons().add(new Image(Objects.requireNonNull(getClass().getResourceAsStream("/icons/icon.png"))));
    stage.setTitle("osci-render");
    Scene scene = new Scene(root);
    scene.addEventFilter(MouseEvent.MOUSE_MOVED, event -> {
      if (controller.mouseRotate()) {
        controller.setObjRotate(new Vector3(
          3 * Math.PI * (event.getSceneY() / scene.getHeight()),
          3 * Math.PI * (event.getSceneX() /  scene.getWidth()),
          0
        ));
      }
    });
    scene.addEventHandler(KeyEvent.KEY_PRESSED, t -> {
      if (t.getCode() == KeyCode.ESCAPE) {
        controller.disableMouseRotate();
      }
    });
    stage.setScene(scene);
    stage.setResizable(false);

    controller.setStage(stage);

    stage.show();

    stage.setOnCloseRequest(t -> {
      Platform.exit();
      System.exit(0);
    });
  }

  public static void main(String[] args) {
    launch(args);
  }
}
