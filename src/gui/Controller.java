package gui;

import audio.AudioPlayer;
import audio.FrameProducer;
import java.io.File;
import java.io.IOException;
import java.net.URL;
import java.util.List;
import java.util.ResourceBundle;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.BlockingQueue;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.Slider;
import javafx.scene.layout.AnchorPane;
import javafx.scene.text.Text;
import javafx.stage.FileChooser;
import javafx.stage.Stage;
import javax.xml.parsers.ParserConfigurationException;
import javax.xml.xpath.XPathEvaluationResult;
import org.xml.sax.SAXException;
import parser.FileParser;
import parser.ObjParser;
import parser.TextParser;
import parser.svg.SvgParser;
import shapes.Shape;
import shapes.Vector2;

public class Controller implements Initializable {

  private static final int BUFFER_SIZE = 20;
  private static final int SAMPLE_RATE = 192000;

  private final FileChooser fileChooser = new FileChooser();
  private final BlockingQueue<List<Shape>> frameQueue = new ArrayBlockingQueue<>(BUFFER_SIZE);
  private final AudioPlayer player = new AudioPlayer(SAMPLE_RATE, frameQueue);

  /* Default parser. */
  private final FrameProducer producer = new FrameProducer(frameQueue);
  private Stage stage;

  @FXML
  private Button btnBrowse;
  @FXML
  private Text sliderOutput;
  @FXML
  private Slider slider;

  public Controller() throws IOException, ParserConfigurationException, SAXException {
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    slider.valueProperty().addListener((source, oldValue, newValue) -> {
      sliderOutput.setText(String.valueOf(slider.getValue()));
      producer.setFocalLength((float) slider.getValue());
    });

    btnBrowse.setOnAction(e -> {
      File file = fileChooser.showOpenDialog(stage);
      try {
        producer.setParser(file.getAbsolutePath());
      } catch (IOException | ParserConfigurationException | SAXException ioException) {
        ioException.printStackTrace();
      }
    });

    new Thread(producer).start();
    new Thread(new AudioPlayer(SAMPLE_RATE, frameQueue)).start();
  }

  public void setStage(Stage stage) {
    this.stage = stage;
  }
}
