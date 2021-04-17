package sh.ball.audio;

import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.Renderer;

import java.io.IOException;

import sh.ball.parser.FileParser;

import javax.xml.parsers.ParserConfigurationException;

public class FrameProducer<T> implements Runnable {

  private final Renderer<T> renderer;
  private final FrameSet<T> frames;

  private boolean running;

  public FrameProducer(Renderer<T> renderer, FileParser<FrameSet<T>> parser) throws IOException, SAXException, ParserConfigurationException {
    this.renderer = renderer;
    this.frames = parser.parse();
  }

  @Override
  public void run() {
    running = true;
    while (running) {
      renderer.addFrame(frames.next());
    }
  }

  public void stop() {
    running = false;
  }

  public void setFrameSettings(Object settings) {
    frames.setFrameSettings(settings);
  }
}
