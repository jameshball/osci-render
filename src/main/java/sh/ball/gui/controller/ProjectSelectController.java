package sh.ball.gui.controller;

import javafx.application.Platform;
import javafx.collections.FXCollections;
import javafx.collections.ObservableList;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ListView;
import javafx.scene.control.ProgressIndicator;
import sh.ball.gui.ExceptionRunnable;

import java.net.URL;
import java.util.ResourceBundle;
import java.util.prefs.Preferences;

public class ProjectSelectController implements Initializable {

  private static final int MAX_ITEMS = 20;
  private static final String RECENT_FILE = "RECENT_FILE_";

  private final Preferences userPreferences = Preferences.userNodeForPackage(getClass());
  private final ObservableList<String> recentFiles = FXCollections.observableArrayList();
  private ExceptionRunnable launchMainApplication;

  @FXML
  private ListView<String> recentFilesListView;
  @FXML
  private Button newProjectButton;
  @FXML
  private CheckBox startMutedCheckBox;

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

    recentFilesListView.setItems(recentFiles);

    newProjectButton.setOnAction(e -> {
      try {
        launchMainApplication.run();
      } catch (Exception ex) {
        throw new RuntimeException(ex);
      }
    });
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

  public void setApplicationLauncher(ExceptionRunnable launchMainApplication) {
    this.launchMainApplication = launchMainApplication;
  }
}
