package sh.ball.gui.controller;

import javafx.application.Platform;
import javafx.beans.InvalidationListener;
import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.audio.FrameProducer;
import sh.ball.audio.effect.*;
import sh.ball.engine.Vector3;
import sh.ball.gui.components.EffectComponentGroup;
import sh.ball.parser.obj.ObjSettingsFactory;
import sh.ball.shapes.Shape;
import sh.ball.shapes.Vector2;

import java.net.URL;
import java.util.*;

import static sh.ball.audio.effect.EffectAnimator.DEFAULT_SAMPLE_RATE;
import static sh.ball.gui.Gui.audioPlayer;

public class ObjController implements Initializable, SubController {

  private FrameProducer<List<Shape>> producer;

  @FXML
  private EffectComponentGroup focalLength;
  @FXML
  private EffectComponentGroup rotateX;
  @FXML
  private EffectComponentGroup rotateY;
  @FXML
  private EffectComponentGroup rotateZ;
  @FXML
  private EffectComponentGroup rotateSpeed;
  @FXML
  private CheckBox rotateCheckBox;
  @FXML
  private Button resetObjectRotationButton;

  private List<EffectComponentGroup> effectComponentGroups() {
    return List.of(focalLength, rotateX, rotateY, rotateZ, rotateSpeed);
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    Map<SVGPath, Slider> map = new HashMap<>();
    effectComponentGroups().forEach(ecg -> map.putAll(ecg.getMidiButtonMap()));
    return map;
  }

  public void updateFocalLength() {
    setFocalLength(focalLength.getValue());
  }

  public void setAudioProducer(FrameProducer<List<Shape>> producer) {
    this.producer = producer;
  }

  // changes the focalLength of the FrameProducer
  public void setFocalLength(double focalLength) {
    if (producer != null) {
      producer.setFrameSettings(ObjSettingsFactory.focalLength(focalLength));
    }
  }

  public void updateObjectRotateSpeed() {
    setObjectRotateSpeed(rotateSpeed.getValue());
  }

  private double linearSpeedToActualSpeed(double rotateSpeed) {
    return (Math.exp(3 * Math.min(10, Math.abs(rotateSpeed))) - 1) / 50;
  }

  // changes the rotateSpeed of the FrameProducer
  private void setObjectRotateSpeed(double rotateSpeed) {
    if (producer != null) {
      double actualSpeed = linearSpeedToActualSpeed(rotateSpeed);
      producer.setFrameSettings(
        ObjSettingsFactory.rotateSpeed(rotateSpeed > 0 ? actualSpeed : -actualSpeed)
      );
    }
  }

  public void setRotateXY(Vector2 rotate) {
    rotateX.setValue(rotate.getX());
    rotateY.setValue(rotate.getY());
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

  // updates the 3D object X angle
  private void setObjRotateX(double x) {
    if (producer != null) {
      producer.setFrameSettings(ObjSettingsFactory.baseRotationX(x));
    }
  }

  // updates the 3D object Y angle
  private void setObjRotateY(double y) {
    if (producer != null) {
      producer.setFrameSettings(ObjSettingsFactory.baseRotationY(y));
    }
  }

  // updates the 3D object Z angle
  private void setObjRotateZ(double z) {
    if (producer != null) {
      producer.setFrameSettings(ObjSettingsFactory.baseRotationZ(z));
    }
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
    resetObjectRotationButton.setOnAction(e -> {
      List.of(rotateX, rotateY, rotateZ, rotateSpeed).forEach(ecg -> {
        ecg.setValue(0);
        ecg.setAnimationType(AnimationType.STATIC);
      });

      setObjRotate(new Vector3(), new Vector3());
    });

    focalLength.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(this::setFocalLength)));
    rotateX.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(this::setObjRotateX)));
    rotateY.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(this::setObjRotateY)));
    rotateZ.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(this::setObjRotateZ)));
    rotateSpeed.setAnimator(new EffectAnimator(DEFAULT_SAMPLE_RATE, new ConsumerEffect(this::setObjectRotateSpeed)));

    effectComponentGroups().forEach(effect -> effect.setEffectUpdater(this::updateEffect));
  }

  // selects or deselects the given audio effect
  public void updateEffect(EffectType type, boolean checked, SettableEffect effect) {
    if (type != null) {
      if (checked) {
        audioPlayer.addEffect(type, effect);
      } else {
        audioPlayer.removeEffect(type);
      }
    }
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return effectComponentGroups().stream().map(EffectComponentGroup::isMicSelectedProperty).toList();
  }

  @Override
  public List<Slider> sliders() {
    return effectComponentGroups().stream().map(EffectComponentGroup::sliders).flatMap(List::stream).toList();
  }

  @Override
  public List<String> labels() {
    return effectComponentGroups().stream().map(EffectComponentGroup::getLabel).toList();
  }

  @Override
  public List<Element> save(Document document) {
    Element element = document.createElement("objController");
    effectComponentGroups().forEach(effect -> effect.save(document).forEach(element::appendChild));
    return List.of(element);
  }

  @Override
  public void load(Element root) {
    Element element = (Element) root.getElementsByTagName("objController").item(0);
    if (element != null) {
      effectComponentGroups().forEach(effect -> effect.load(element));
    } else {
      effectComponentGroups().forEach(effect -> effect.setAnimationType(AnimationType.STATIC));
    }
  }

  @Override
  public void micNotAvailable() {
    effectComponentGroups().forEach(EffectComponentGroup::micNotAvailable);
  }

  public void renderUsingGpu(boolean usingGpu) {
    producer.setFrameSettings(ObjSettingsFactory.renderUsingGpu(usingGpu));
  }
}
