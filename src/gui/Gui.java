package gui;


import javafx.application.Application;
import javafx.application.Platform;
import javafx.fxml.FXMLLoader;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.stage.Stage;

public class Gui extends Application {

  @Override
  public void start(Stage primaryStage) throws Exception {
    FXMLLoader loader = new FXMLLoader(getClass().getResource("/fxml/osci-render.fxml"));
    Parent root = loader.load();
    Controller controller = loader.getController();
    controller.setStage(primaryStage);

    primaryStage.setTitle("osci-render");
    primaryStage.setScene(new Scene(root));

    controller.setStage(primaryStage);

    primaryStage.show();

    primaryStage.setOnCloseRequest(t -> {
      Platform.exit();
      System.exit(0);
    });
  }

  public static void main(String[] args) {
    launch(args);
  }
}
