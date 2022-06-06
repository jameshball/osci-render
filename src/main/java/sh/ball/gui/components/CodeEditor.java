// FROM https://gist.github.com/jewelsea/1463485 BY USER https://gist.github.com/jewelsea

package sh.ball.gui.components;

import javafx.scene.layout.StackPane;
import javafx.scene.web.WebView;

import java.io.IOException;
import java.net.URISyntaxException;
import java.nio.file.Files;
import java.nio.file.Path;

/**
 * A syntax highlighting code editor for JavaFX created by wrapping a
 * CodeMirror code editor in a WebView.
 *
 * See http://codemirror.net for more information on using the codemirror editor.
 */
public class CodeEditor extends StackPane {
  /** a webview used to encapsulate the CodeMirror JavaScript. */
  final WebView webview = new WebView();

  /** a snapshot of the code to be edited kept for easy initilization and reversion of editable code. */
  private String editingCode;

  /** applies the editing template to the editing code to create the html+javascript source for a code editor. */
  private String applyEditingTemplate() throws URISyntaxException, IOException {
    String template = Files.readString(Path.of(getClass().getResource("/html/code_editor.html").toURI()));
    return template.replace("${code}", editingCode);
  }

  /** sets the current code in the editor and creates an editing snapshot of the code which can be reverted to. */
  public void setCode(String newCode) throws URISyntaxException, IOException {
    this.editingCode = newCode;
    webview.getEngine().loadContent(applyEditingTemplate());
  }

  /** returns the current code in the editor and updates an editing snapshot of the code which can be reverted to. */
  public String getCodeAndSnapshot() {
    this.editingCode = (String) webview.getEngine().executeScript("editor.getValue();");
    return editingCode;
  }

  /**
   * Create a new code editor.
   * @param editingCode the initial code to be edited in the code editor.
   */
  public CodeEditor(String editingCode) throws URISyntaxException, IOException {
    this.editingCode = editingCode;

    webview.getEngine().loadContent(applyEditingTemplate());

    this.getChildren().add(webview);
  }
}