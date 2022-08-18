package sh.ball.gui.controller;

import javafx.beans.property.BooleanProperty;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import sh.ball.gui.components.EffectComponentGroup;

import java.util.List;
import java.util.Map;

public interface SubController {

  Map<SVGPath, Slider> getMidiButtonMap();

  List<BooleanProperty> micSelected();

  List<Slider> sliders();

  List<String> labels();

  List<EffectComponentGroup> effects();

  List<? extends Node> save(Document document);

  void load(Element root);

  void micNotAvailable();
}
