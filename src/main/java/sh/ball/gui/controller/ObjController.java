package sh.ball.gui.controller;

import javafx.application.Platform;
import javafx.beans.InvalidationListener;
import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.paint.Color;
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

  private boolean setFixedAngleX = false;
  private boolean setFixedAngleY = false;
  private boolean setFixedAngleZ = false;

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
  @FXML
  private SVGPath fixedAngleX;
  @FXML
  private SVGPath fixedAngleY;
  @FXML
  private SVGPath fixedAngleZ;

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    Map<SVGPath, Slider> map = new HashMap<>();
    effects().forEach(effect -> map.putAll(effect.getMidiButtonMap()));
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
      if (setFixedAngleX) {
        producer.setFrameSettings(ObjSettingsFactory.actualRotationX(x));
      } else {
        producer.setFrameSettings(ObjSettingsFactory.baseRotationX(x));
      }
    }
  }

  // updates the 3D object Y angle
  private void setObjRotateY(double y) {
    if (producer != null) {
      if (setFixedAngleY) {
        producer.setFrameSettings(ObjSettingsFactory.actualRotationY(y));
      } else {
        producer.setFrameSettings(ObjSettingsFactory.baseRotationY(y));
      }
    }
  }

  // updates the 3D object Z angle
  private void setObjRotateZ(double z) {
    if (producer != null) {
      if (setFixedAngleZ) {
        producer.setFrameSettings(ObjSettingsFactory.actualRotationZ(z));
      } else {
        producer.setFrameSettings(ObjSettingsFactory.baseRotationZ(z));
      }
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

    effects().forEach(effect -> effect.setEffectUpdater(audioPlayer::updateEffect));


    // TODO: Remove duplicate code with EffectsController
    fixedAngleX.setOnMouseClicked(e -> {
      setFixedAngleX = !setFixedAngleX;
      if (setFixedAngleX) {
        fixedAngleX.setFill(Color.RED);
      } else {
        fixedAngleX.setFill(Color.WHITE);
      }
      rotateX.getAnimator().updateValue();
    });
    fixedAngleY.setOnMouseClicked(e -> {
      setFixedAngleY = !setFixedAngleY;
      if (setFixedAngleY) {
        fixedAngleY.setFill(Color.RED);
      } else {
        fixedAngleY.setFill(Color.WHITE);
      }
      rotateY.getAnimator().updateValue();
    });
    fixedAngleZ.setOnMouseClicked(e -> {
      setFixedAngleZ = !setFixedAngleZ;
      if (setFixedAngleZ) {
        fixedAngleZ.setFill(Color.RED);
      } else {
        fixedAngleZ.setFill(Color.WHITE);
      }
      rotateZ.getAnimator().updateValue();
    });
  }

  @Override
  public List<BooleanProperty> micSelected() {
    return effects().stream().map(EffectComponentGroup::isMicSelectedProperty).toList();
  }

  @Override
  public List<Slider> sliders() {
    return effects().stream().map(EffectComponentGroup::sliders).flatMap(List::stream).toList();
  }

  @Override
  public List<String> labels() {
    return effects().stream().map(EffectComponentGroup::getLabel).toList();
  }

  @Override
  public List<EffectComponentGroup> effects() {
    return List.of(focalLength, rotateX, rotateY, rotateZ, rotateSpeed);
  }

  @Override
  public List<Element> save(Document document) {
    // TODO: Remove duplication with EffectsController
    Element objectFixedRotate = document.createElement("objectFixedRotate");
    Element fixedRotateX = document.createElement("x");
    fixedRotateX.appendChild(document.createTextNode(String.valueOf(setFixedAngleX)));
    Element fixedRotateY = document.createElement("y");
    fixedRotateY.appendChild(document.createTextNode(String.valueOf(setFixedAngleY)));
    Element fixedRotateZ = document.createElement("z");
    fixedRotateZ.appendChild(document.createTextNode(String.valueOf(setFixedAngleZ)));
    objectFixedRotate.appendChild(fixedRotateX);
    objectFixedRotate.appendChild(fixedRotateY);
    objectFixedRotate.appendChild(fixedRotateZ);

    return List.of(objectFixedRotate);
  }

  @Override
  public void load(Element root) {
    Element objectFixedRotate = (Element) root.getElementsByTagName("objectFixedRotate").item(0);

    if (objectFixedRotate == null) {
      setFixedAngleX = false;
      setFixedAngleY = false;
      setFixedAngleZ = false;
    } else {
      Element fixedRotateX = (Element) objectFixedRotate.getElementsByTagName("x").item(0);
      setFixedAngleX = fixedRotateX != null && Boolean.parseBoolean(fixedRotateX.getTextContent());

      Element fixedRotateY = (Element) objectFixedRotate.getElementsByTagName("y").item(0);
      setFixedAngleY = fixedRotateY != null && Boolean.parseBoolean(fixedRotateY.getTextContent());

      Element fixedRotateZ = (Element) objectFixedRotate.getElementsByTagName("z").item(0);
      setFixedAngleZ = fixedRotateZ != null && Boolean.parseBoolean(fixedRotateZ.getTextContent());
    }

    fixedAngleX.setFill(setFixedAngleX ? Color.RED : Color.WHITE);
    fixedAngleY.setFill(setFixedAngleY ? Color.RED : Color.WHITE);
    fixedAngleZ.setFill(setFixedAngleZ ? Color.RED : Color.WHITE);
  }

  @Override
  public void micNotAvailable() {
    effects().forEach(EffectComponentGroup::micNotAvailable);
  }

  public void renderUsingGpu(boolean usingGpu) {
    producer.setFrameSettings(ObjSettingsFactory.renderUsingGpu(usingGpu));
  }
}
