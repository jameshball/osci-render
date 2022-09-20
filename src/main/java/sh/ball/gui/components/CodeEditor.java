// FROM https://gist.github.com/jewelsea/1463485 BY USER https://gist.github.com/jewelsea

package sh.ball.gui.components;

import com.sun.javafx.webkit.WebConsoleListener;
import javafx.concurrent.Worker;
import javafx.scene.layout.StackPane;
import javafx.scene.web.WebView;
import netscape.javascript.JSObject;

import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.charset.StandardCharsets;
import java.util.function.BiConsumer;

public class CodeEditor extends StackPane {

  private WebView webview;

  private String editingCode = "";
  private String fileName = "";
  private BiConsumer<byte[], String> callback;
  private boolean newFile = true;

  private String applyEditingTemplate() throws IOException {
    String template = new String(getClass().getResourceAsStream("/html/code_editor.html").readAllBytes(), StandardCharsets.UTF_8);
    return template.replace("${code}", editingCode);
  }

  public void setCode(String editingCode, String fileName) {
    this.fileName = fileName;
    this.editingCode = editingCode;
    this.newFile = true;
    JSObject window = (JSObject) webview.getEngine().executeScript("window");
    window.setMember("newCode", editingCode);
    webview.getEngine().executeScript("""
      editor.setValue(newCode);
      editor.clearHistory();
    """);
  }

  public void updateCode() {
    editingCode = (String) webview.getEngine().executeScript("editor.getValue();");
    if (!newFile && callback != null) {
      callback.accept(editingCode.getBytes(StandardCharsets.UTF_8), fileName);
    }
    newFile = false;
  }

  public void setCallback(BiConsumer<byte[], String> callback) {
    this.callback = callback;
  }

  public void setMode(String mode) {
    JSObject window = (JSObject) webview.getEngine().executeScript("window");
    window.setMember("language", mode);
    webview.getEngine().executeScript("editor.setOption(\"mode\", language);");
  }

  public CodeEditor() {}

  public void initialize() throws URISyntaxException, IOException {
    webview = new WebView();
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