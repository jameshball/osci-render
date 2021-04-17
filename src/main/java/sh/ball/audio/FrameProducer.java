package sh.ball.audio;

import org.xml.sax.SAXException;
import sh.ball.FrameSet;
import sh.ball.Renderer;

import java.io.IOException;
import sh.ball.parser.FileParser;

import javax.xml.parsers.ParserConfigurationException;

public class FrameProducer<S> implements Runnable {

  private final Renderer<S> renderer;
  private final FrameSet<S> frames;

  private boolean running;

  public FrameProducer(Renderer<S> renderer, FileParser<FrameSet<S>> parser) throws IOException, SAXException, ParserConfigurationException {
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
