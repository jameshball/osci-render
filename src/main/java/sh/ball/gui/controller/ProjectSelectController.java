package sh.ball.gui.controller;

import javafx.application.Application;
import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.concurrent.Worker;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.*;
import javafx.scene.control.Button;
import javafx.scene.control.Label;
import javafx.scene.layout.VBox;
import javafx.scene.web.WebView;
import javafx.stage.FileChooser;
import javafx.stage.Stage;
import netscape.javascript.JSObject;
import org.w3c.dom.Document;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;
import org.w3c.dom.events.EventTarget;
import org.w3c.dom.html.HTMLAnchorElement;
import sh.ball.gui.ExceptionBiConsumer;
import sh.ball.gui.GitHubReleaseDetector;
import sh.ball.gui.Gui;

import java.awt.*;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.ResourceBundle;
import java.util.function.Consumer;
import java.util.logging.Level;
import java.util.prefs.Preferences;

import static sh.ball.gui.Gui.LOG_DIR;
import static sh.ball.gui.Gui.logger;

public class ProjectSelectController implements Initializable {

  private static final int MAX_ITEMS = 20;
  private static final String RECENT_FILE = "RECENT_FILE_";
  private static final String START_MUTED = "START_MUTED";

  private final Preferences userPreferences = Preferences.userNodeForPackage(getClass());
  private final ObservableList<String> recentFiles = FXCollections.observableArrayList();
  private final GitHubReleaseDetector gitHubReleaseDetector = new GitHubReleaseDetector("jameshball", "osci-render");
  private ExceptionBiConsumer<String, Boolean> launchMainApplication;
  private Consumer<String> openBrowser;

  private Stage stage;
  private final FileChooser projectFileChooser = new FileChooser();
  private final Hyperlink latestReleaseHyperlink = new Hyperlink();

  @FXML
  private ListView<String> recentFilesListView;
  @FXML
  private Button newProjectButton;
  @FXML
  private Button openProjectButton;
  @FXML
  private CheckBox startMutedCheckBox;
  @FXML
  private WebView changelogWebView;
  @FXML
  private Button logButton;
  @FXML
  private Label versionLabel;
  @FXML
  private VBox projectVBox;

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    for (int i = 0; i < MAX_ITEMS; i++) {
      String path = userPreferences.get(RECENT_FILE + i, null);
      if (path != null) {
        recentFiles.add(path);
      } else {
        break;
      }
    }

    projectFileChooser.getExtensionFilters().add(
      new FileChooser.ExtensionFilter("osci-render files", "*.osci")
    );

    latestReleaseHyperlink.setOnAction(event -> openBrowser.accept("https://github.com/jameshball/osci-render/releases/latest"));
    new Thread(() -> {
      String latestRelease = gitHubReleaseDetector.getLatestRelease();
      Platform.runLater(() -> {
        if (!latestRelease.equals(versionLabel.getText())) {
          latestReleaseHyperlink.setText(latestRelease + " is the latest version on GitHub!");
          projectVBox.getChildren().add(projectVBox.getChildren().size() - 1, latestReleaseHyperlink);
        }
      });
    }).start();

    openProjectButton.setOnAction(e -> {
      File file = projectFileChooser.showOpenDialog(stage);
      if (file != null) {
        try {
          launchMainApplication.accept(file.getAbsolutePath(), startMutedCheckBox.isSelected());
        } catch (Exception ex) {
          logger.log(Level.SEVERE, ex.getMessage(), ex);
        }
      }
    });

    recentFilesListView.setItems(recentFiles);
    recentFilesListView.setOnMouseClicked(e -> {
      try {
        String selectedItem = recentFilesListView.getSelectionModel().getSelectedItem();
        if (selectedItem != null) {
          if (!new File(selectedItem).exists()) {
            removeRecentFile(selectedItem);
            return;
          }
          launchMainApplication.accept(selectedItem, startMutedCheckBox.isSelected());
        }
      } catch (Exception ex) {
        logger.log(Level.SEVERE, ex.getMessage(), ex);
      }
    });

    newProjectButton.setOnAction(e -> {
      try {
        launchMainApplication.accept(null, startMutedCheckBox.isSelected());
      } catch (Exception ex) {
        logger.log(Level.SEVERE, ex.getMessage(), ex);
      }
    });

    startMutedCheckBox.setSelected(userPreferences.getBoolean(START_MUTED, false));
    startMutedCheckBox.selectedProperty().addListener((e, old, startMuted) -> userPreferences.putBoolean(START_MUTED, startMuted));

    logButton.setOnAction(e -> {
      try {
        Gui.openFileExplorer(LOG_DIR);
      } catch (IOException ex) {
        logger.log(Level.SEVERE, ex.getMessage(), ex);
      }
    });

    try {
      String changelogHtml = new String(getClass().getResourceAsStream("/html/changelog.html").readAllBytes(), StandardCharsets.UTF_8);
      changelogWebView.getEngine().loadContent(changelogHtml);
      InputStream changelogInputStream = getClass().getResourceAsStream("/CHANGELOG.md");
      String changelog = new String(changelogInputStream.readAllBytes(), StandardCharsets.UTF_8);
      changelogWebView.getEngine().getLoadWorker().stateProperty().addListener((e, old, state) -> {
        if (state == Worker.State.SUCCEEDED) {
          JSObject window = (JSObject) changelogWebView.getEngine().executeScript("window");
          window.setMember("changelog", changelog);
          changelogWebView.getEngine().executeScript(
            "document.getElementById('content').innerHTML = marked.parse(changelog);"
          );
          Document document = changelogWebView.getEngine().getDocument();
          NodeList nodeList = document.getElementsByTagName("a");
          for (int i = 0; i < nodeList.getLength(); i++) {
            Node node= nodeList.item(i);
            EventTarget eventTarget = (EventTarget) node;
            eventTarget.addEventListener("click", evt -> {
              EventTarget target = evt.getCurrentTarget();
              HTMLAnchorElement anchorElement = (HTMLAnchorElement) target;
              String href = anchorElement.getHref();
              openBrowser.accept(href);
              evt.preventDefault();
            }, false);
          }
        }
      });
    } catch (IOException e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
    }
  }

  public void addRecentFile(String path) {
    int index = recentFiles.indexOf(path);
    if (index == -1) {
      userPreferences.get(RECENT_FILE + recentFiles.size(),  path);
      recentFiles.add(0, path);
      if (recentFiles.size() > MAX_ITEMS) {
        recentFiles.remove(recentFiles.size() - 1);
      }
    } else {
      recentFiles.remove(index);
      recentFiles.add(0, path);
    }
    resetRecentFiles();
  }

  private void resetRecentFiles() {
    for (int i = 0; i < MAX_ITEMS; i++) {
      userPreferences.remove(RECENT_FILE + i);
    }

    for (int i = 0; i < recentFiles.size(); i++) {
      userPreferences.put(RECENT_FILE + i, recentFiles.get(i));
    }
  }

  public List<String> recentFiles() {
    List<String> paths = new ArrayList<>();

    for (int i = 0; i < MAX_ITEMS; i++) {
      String path = userPreferences.get(RECENT_FILE + i, null);
      if (path == null) {
        return paths;
      }
      paths.add(path);
    }

    return paths;
  }

  public void setApplicationLauncher(ExceptionBiConsumer<String, Boolean> launchMainApplication) {
    this.launchMainApplication = launchMainApplication;
  }

  public void setOpenBrowser(Consumer<String> openBrowser) {
    this.openBrowser = openBrowser;
  }

  public void removeRecentFile(String path) {
    recentFiles.remove(path);
    resetRecentFiles();
  }

  public String version() {
    return versionLabel.getText();
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }
}
