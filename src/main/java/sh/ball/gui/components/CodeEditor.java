// FROM https://gist.github.com/jewelsea/1463485 BY USER https://gist.github.com/jewelsea

package sh.ball.gui.components;

import com.sun.javafx.webkit.WebConsoleListener;
import javafx.concurrent.Worker;
import javafx.scene.layout.StackPane;
import javafx.scene.web.WebView;
import netscape.javascript.JSObject;

import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;

public class CodeEditor extends StackPane {

  final WebView webview = new WebView();

  private String editingCode;

  private String applyEditingTemplate() throws URISyntaxException, IOException {
    String template = Files.readString(Path.of(getClass().getResource("/html/code_editor.html").toURI()));
    return template.replace("${code}", editingCode);
  }

  public void updateCode() {
    this.editingCode = (String) webview.getEngine().executeScript("editor.getValue();");
    System.out.println(editingCode);
  }

  public CodeEditor(String editingCode) throws URISyntaxException, IOException {
    this.editingCode = editingCode;

    webview.getEngine().getLoadWorker().stateProperty().addListener((e, old, state) -> {
      if (state == Worker.State.SUCCEEDED) {
        JSObject window = (JSObject) webview.getEngine().executeScript("window");
        window.setMember("javaCodeEditor", this);
      }
    });

    webview.getEngine().loadContent(applyEditingTemplate());

    WebConsoleListener.setDefaultListener((webView, message, lineNumber, sourceId) ->
      System.out.println(message + "[at " + lineNumber + "]")
    );

    this.getChildren().add(webview);
  }
}