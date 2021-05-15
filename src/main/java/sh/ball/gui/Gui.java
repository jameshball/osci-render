package sh.ball.gui;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.image.Image;
import javafx.stage.Stage;
import sh.ball.audio.AudioPlayer;

import java.util.Objects;

public class Gui extends Application {

  private static final int SAMPLE_RATE = 192000;

  @Override
  public void start(Stage stage) throws Exception {
    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/osci-render.fxml"));
    Controller controller = new Controller(new AudioPlayer(SAMPLE_RATE));
    loader.setController(controller);
    Parent root = loader.load();

    stage.getIcons().add(new Image(Objects.requireNonNull(getClass().getResourceAsStream("/icons/icon.png"))));
    stage.setTitle("osci-render");
    stage.setScene(new Scene(root));
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
