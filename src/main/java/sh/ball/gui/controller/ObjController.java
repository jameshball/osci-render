package sh.ball.gui.controller;

import javafx.beans.InvalidationListener;
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
import sh.ball.shapes.Vector2;

import java.net.URL;
import java.util.*;

public class ObjController implements Initializable, SubController {

  private FrameProducer<List<Shape>> producer;

  @FXML
  private Slider focalLengthSlider;
  @FXML
  private SVGPath focalLengthMidi;
  @FXML
  private Slider objectXRotateSlider;
  @FXML
  private SVGPath objectXRotateMidi;
  @FXML
  private CheckBox objectXRotateMic;
  @FXML
  private Slider objectYRotateSlider;
  @FXML
  private SVGPath objectYRotateMidi;
  @FXML
  private CheckBox objectYRotateMic;
  @FXML
  private Slider objectZRotateSlider;
  @FXML
  private SVGPath objectZRotateMidi;
  @FXML
  private CheckBox objectZRotateMic;
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
      objectXRotateMidi, objectXRotateSlider,
      objectYRotateMidi, objectYRotateSlider,
      objectZRotateMidi, objectZRotateSlider,
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

  private double linearSpeedToActualSpeed(double rotateSpeed) {
    return (Math.exp(3 * Math.min(10, Math.abs(rotateSpeed))) - 1) / 50;
  }

  // changes the rotateSpeed of the FrameProducer
  private void setObjectRotateSpeed(double rotateSpeed) {
    double actualSpeed = linearSpeedToActualSpeed(rotateSpeed);
    producer.setFrameSettings(
      ObjSettingsFactory.rotateSpeed(rotateSpeed > 0 ? actualSpeed : -actualSpeed)
    );
  }

  public void setRotateXY(Vector2 rotate) {
    objectXRotateSlider.setValue(rotate.getX());
    objectYRotateSlider.setValue(rotate.getY());
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
  private void setObjRotate(Vector3 vector) {
    producer.setFrameSettings(ObjSettingsFactory.baseRotation(vector));
  }

  // updates the 3D object base and current rotation angle
  public void setObjRotate(Vector3 baseRotation, Vector3 currentRotation) {
    producer.setFrameSettings(ObjSettingsFactory.rotation(baseRotation, currentRotation));
  }

  public void hideHiddenMeshes(Boolean hidden) {
    producer.setFrameSettings(ObjSettingsFactory.hideEdges(hidden));
  }

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    focalLengthSlider.valueProperty().addListener((source, oldValue, newValue) ->
      setFocalLength(newValue.doubleValue())
    );
    InvalidationListener rotateSpeedListener = e -> setObjRotate(new Vector3(
      objectXRotateSlider.getValue() * Math.PI,
      objectYRotateSlider.getValue() * Math.PI,
      objectZRotateSlider.getValue() * Math.PI
    ));
    objectXRotateSlider.valueProperty().addListener(rotateSpeedListener);
    objectYRotateSlider.valueProperty().addListener(rotateSpeedListener);
    objectZRotateSlider.valueProperty().addListener(rotateSpeedListener);

    resetObjectRotationButton.setOnAction(e -> {
      objectXRotateSlider.setValue(0);
      objectYRotateSlider.setValue(0);
      objectZRotateSlider.setValue(0);
      objectRotateSpeedSlider.setValue(0);
      setObjRotate(new Vector3(), new Vector3());
    });

    objectRotateSpeedSlider.valueProperty().addListener((e, old, speed) -> {
      setObjectRotateSpeed(speed.doubleValue());
    });
  }

  @Override
  public List<CheckBox> micCheckBoxes() {
    List<CheckBox> checkboxes = new ArrayList<>();
    checkboxes.add(null);
    checkboxes.add(objectXRotateMic);
    checkboxes.add(objectYRotateMic);
    checkboxes.add(objectZRotateMic);
    checkboxes.add(objectRotateSpeedMic);
    return checkboxes;
  }

  @Override
  public List<Slider> sliders() {
    return List.of(focalLengthSlider, objectXRotateSlider, objectYRotateSlider,
      objectZRotateSlider, objectRotateSpeedSlider);
  }

  @Override
  public List<String> labels() {
    return List.of("focalLength", "objectXRotate", "objectYRotate", "objectZRotate",
      "objectRotateSpeed");
  }

  @Override
  public Element save(Document document) {
    return document.createElement("null");
  }

  @Override
  public void load(Element root) {
  }
}
