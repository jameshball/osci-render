package sh.ball.gui.controller;

import com.sun.javafx.geom.RoundRectangle2D;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.fxml.FXML;
import javafx.fxml.Initializable;
import javafx.scene.canvas.Canvas;
import javafx.scene.control.Slider;
import javafx.scene.image.PixelFormat;
import javafx.scene.image.PixelWriter;
import javafx.scene.image.WritablePixelFormat;
import javafx.scene.shape.Circle;
import javafx.scene.shape.Rectangle;
import javafx.scene.shape.SVGPath;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import sh.ball.gui.components.EffectComponentGroup;

import java.net.URL;
import java.nio.ByteBuffer;
import java.util.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.audioPlayer;
import static sh.ball.gui.Gui.logger;

public class VolumeController implements Initializable, SubController {
  @FXML
  private Slider volumeSlider;
  @FXML
  private Slider thresholdSlider;
  @FXML
  private Canvas volumeCanvas;

  public VolumeController() {}

  @Override
  public void initialize(URL url, ResourceBundle resourceBundle) {
    volumeSlider.valueProperty().addListener((o, old, volume) -> audioPlayer.setVolume(volume.doubleValue() / 3));
    thresholdSlider.valueProperty().addListener((o, old, threshold) -> audioPlayer.setThreshold(threshold.doubleValue()));

    PixelWriter pixelWriter = volumeCanvas.getGraphicsContext2D().getPixelWriter();
    WritablePixelFormat<ByteBuffer> byteBgraInstance = PixelFormat.getByteBgraPreInstance();

    int width = (int) volumeCanvas.widthProperty().get();
    int height = (int) volumeCanvas.heightProperty().get();

    byte[] pixels = new byte[width * height * 4];
    for (int i = 0; i < pixels.length; i += 4) {
      pixels[i] = (byte) 0xFF;
      pixels[i + 1] = (byte) 0xFF;
      pixels[i + 2] = (byte) 0xFF;
      pixels[i + 3] = (byte) 0xFF;
    }
    pixelWriter.setPixels(0, 0, width, height, byteBgraInstance, pixels, 0, width * 4);

    double[] buf = new double[2 << 11];
    double[] leftVolumes = new double[2 << 5];
    double[] rightVolumes = new double[2 << 5];

    new Thread(() -> {
      double avgLeftVolume = 0;
      double avgRightVolume = 0;

      while (true) {
        try {
          audioPlayer.read(buf);
        } catch (InterruptedException e) {
          logger.log(Level.SEVERE, e.getMessage(), e);
        }

        double leftVolume = 0;
        double rightVolume = 0;

        for (int i = 0; i < buf.length; i += 2) {
          leftVolume += buf[i] * buf[i];
          rightVolume += buf[i + 1] * buf[i + 1];
        }
        // root-mean-square
        leftVolume = Math.sqrt(leftVolume / (buf.length / 2.0));
        rightVolume = Math.sqrt(rightVolume / (buf.length / 2.0));

        avgLeftVolume = avgLeftVolume - leftVolumes[0] / leftVolumes.length + leftVolume / leftVolumes.length;
        System.arraycopy(leftVolumes, 1, leftVolumes, 0, leftVolumes.length - 1);
        leftVolumes[leftVolumes.length - 1] = leftVolume;

        avgRightVolume = avgRightVolume - rightVolumes[0] / rightVolumes.length + rightVolume / rightVolumes.length;
        System.arraycopy(rightVolumes, 1, rightVolumes, 0, rightVolumes.length - 1);
        rightVolumes[rightVolumes.length - 1] = rightVolume;

        double leftThreshold = height - leftVolume * height - 1;
        double rightThreshold = height - rightVolume * height - 1;

        double leftBar = height - avgLeftVolume * height - 1;
        double rightBar = height - avgRightVolume * height - 1;
        double barWidth = 2;

        double threshold = height - thresholdSlider.getValue() * height - 1;

        for (int i = 0; i < pixels.length; i += 4) {
          int pixelNum = i / 4;
          int x = pixelNum % width;
          int y = pixelNum / width;

          boolean drawGreen = x < width / 2 && y > leftThreshold || x >= width / 2 && y > rightThreshold;
          boolean drawBar = x < width / 2 && y >= leftBar - barWidth && y <= leftBar + barWidth || x >= width / 2 && y >= rightBar - barWidth && y <= rightBar + barWidth;
          boolean drawThreshold = y >= threshold - barWidth && y <= threshold + barWidth;

          byte r = (byte) 0xFF;
          byte g = (byte) 0xFF;
          byte b = (byte) 0xFF;
          byte a = (byte) 0xFF;

          if (drawBar || drawThreshold) {
            r = (byte) 0x00;
            g = (byte) 0x00;
            b = (byte) 0x00;
          } else if (drawGreen) {
            r = (byte) (0xFF - 0xFF * ((double) y / height));
            g = (byte) (0xFF * ((double) y / height));
            b = (byte) 0x00;
          }

          pixels[i] = b;
          pixels[i + 1] = g;
          pixels[i + 2] = r;
          pixels[i + 3] = a;
        }

        Platform.runLater(() -> pixelWriter.setPixels(0, 0, width, height, byteBgraInstance, pixels, 0, width * 4));
      }
    }).start();
  }

  @Override
  public Map<SVGPath, Slider> getMidiButtonMap() {
    return Map.of();
  }

  @Override
  public List<BooleanProperty> micSelected() {
    List<BooleanProperty> micSelected = new ArrayList<>();
    micSelected.add(null);
    micSelected.add(null);
    return micSelected;
  }

  @Override
  public List<Slider> sliders() {
    return List.of(volumeSlider, thresholdSlider);
  }

  @Override
  public List<String> labels() {
    return List.of("volume", "threshold");
  }

  @Override
  public List<EffectComponentGroup> effects() {
    return List.of();
  }

  @Override
  public List<Element> save(Document document) {
    return List.of(document.createElement("null"));
  }

  @Override
  public void load(Element root) {}

  @Override
  public void micNotAvailable() {}

  public void setVolume(double volume) {
    volumeSlider.setValue(volume);
  }
}
