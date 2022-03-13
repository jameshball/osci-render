package sh.ball.gui.controller;

import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.FrameProducer;
import sh.ball.engine.Vector3;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.shapes.Shape;

import java.net.URL;
import java.util.*;

public class ObjController implements Initializable, SubController {

  private Vector3 rotation = new Vector3(2 * Math.PI, 2 * Math.PI, 0);
  private FrameProducer<List<Shape>> producer;

  @FXML
  private Slider focalLengthSlider;
  @FXML
  private SVGPath focalLengthMidi;
  @FXML
  private Slider objectRotateSpeedSlider;
  @FXML
  private SVGPath objectRotateSpeedMidi;
  @FXML
  private CheckBox objectRotateSpeedMic;
  @FXML
  private CheckBox rotateCheckBox;
  @FXML
  private Button resetObjectRotationButton;

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of(
      focalLengthMidi, focalLengthSlider,
      objectRotateSpeedMidi, objectRotateSpeedSlider
    );
  }

  public void updateFocalLength() {
    setFocalLength(focalLengthSlider.getValue());
  }

  public void setAudioProducer(FrameProducer<List<Shape>> producer) {
    this.producer = producer;
  }

  // changes the focalLength of the FrameProducer
  public void setFocalLength(double focalLength) {
    producer.setFrameSettings(ObjSettingsFactory.focalLength(focalLength));
  }

  public void updateObjectRotateSpeed() {
    setObjectRotateSpeed(objectRotateSpeedSlider.getValue());
  }

  // changes the rotateSpeed of the FrameProducer
  public void setObjectRotateSpeed(double rotateSpeed) {
    double actualSpeed = (Math.exp(3 * Math.min(10, Math.abs(rotateSpeed))) - 1) / 50;
    producer.setFrameSettings(
      ObjSettingsFactory.rotateSpeed(rotateSpeed > 0 ? actualSpeed : -actualSpeed)
    );
  }

  // determines whether the mouse is being used to rotate a 3D object
  public boolean mouseRotate() {
    return rotateCheckBox.isSelected();
  }

  // stops the mouse rotating the 3D object when ESC is pressed or checkbox is
  // unchecked
  public void disableMouseRotate() {
    rotateCheckBox.setSelected(false);
  }

  // updates the 3D object base rotation angle
  public void setObjRotate(Vector3 vector) {
    rotation = vector;
    producer.setFrameSettings(ObjSettingsFactory.baseRotation(vector));
  }

  // updates the 3D object base and current rotation angle
  public void setObjRotate(Vector3 baseRotation, Vector3 currentRotation) {
    producer.setFrameSettings(ObjSettingsFactory.rotation(baseRotation, currentRotation));
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    focalLengthSlider.valueProperty().addListener((source, oldValue, newValue) ->
      setFocalLength(newValue.doubleValue())
    );
    objectRotateSpeedSlider.valueProperty().addListener((source, oldValue, newValue) ->
      setObjectRotateSpeed(newValue.doubleValue())
    );
    resetObjectRotationButton.setOnAction(e -> setObjRotate(new Vector3(2 * Math.PI, 2 * Math.PI, 0), new Vector3()));
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    List<CheckBox> checkboxes = new ArrayList<>();
    checkboxes.add(null);
    checkboxes.add(objectRotateSpeedMic);
    return checkboxes;
  }

  @Override
  public List<Slider> sliders() {
    return List.of(focalLengthSlider, objectRotateSpeedSlider);
  }

  @Override
  public List<String> labels() {
    return List.of("focalLength", "objectRotateSpeed");
  }

  @Override
  public Element save(Document document) {
    Element element = document.createElement("objectRotation");
    Element x = document.createElement("x");
    x.appendChild(document.createTextNode(Double.toString(rotation.getX())));
    Element y = document.createElement("y");
    y.appendChild(document.createTextNode(Double.toString(rotation.getY())));
    Element z = document.createElement("z");
    z.appendChild(document.createTextNode(Double.toString(rotation.getZ())));
    element.appendChild(x);
    element.appendChild(y);
    element.appendChild(z);
    return element;
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("objectRotation").item(0);
    Element x = (Element) element.getElementsByTagName("x").item(0);
    Element y = (Element) element.getElementsByTagName("y").item(0);
    Element z = (Element) element.getElementsByTagName("z").item(0);
    rotation = new Vector3(
      Double.parseDouble(x.getTextContent()),
      Double.parseDouble(y.getTextContent()),
      Double.parseDouble(z.getTextContent())
    );
    setObjRotate(rotation);
  }
}
