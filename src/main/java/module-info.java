module oscirender {
  requires javafx.controls;
  requires javafx.controls.exposed;
  requires javafx.graphics;
  requires javafx.graphics.exposed;
  requires javafx.fxml;
  requires javafx.fxml.exposed;
  requires javafx.base.exposed;
  requires xt.audio;
  requires java.xml;
  requires java.data.front;
  requires org.jgrapht.core;
  requires org.unbescape.html;
  requires java.desktop;

  opens sh.ball.gui;
}