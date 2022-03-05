package sh.ball.gui.controller;

import javafx.scene.control.CheckBox;
import javafx.scene.control.Slider;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;

import java.util.List;
import java.util.Map;

public interface SubController {

  Map<SVGPath, Slider> getMidiButtonMap();

  List<CheckBox> micCheckBoxes();

  List<Slider> sliders();

  List<String> labels();

  Element save(Document document);

  void load(Element root);
}
