package sh.ball.parser.lua;

import sh.ball.audio.FrameSource;
import sh.ball.parser.FileParser;
import sh.ball.shapes.Vector2;

import java.io.*;
import java.util.stream.Collectors;

public class LuaParser extends FileParser<FrameSource<Vector2>> {

  private final LuaSampleSource sampleSource = new LuaSampleSource(LuaExecutor.STANDARD_GLOBALS);

  private String script;

  public void setScriptFromInputStream(InputStream inputStream) {
    this.script = new BufferedReader(new InputStreamReader(inputStream))
      .lines().collect(Collectors.joining("\n"));
  }

  @Override
  public String getFileExtension() {
    return "lua";
  }

  @Override
  public FrameSource<Vector2> parse() throws Exception {
    sampleSource.setScript(script);
    return sampleSource;
  }

  @Override
  public FrameSource<Vector2> get() {
    return sampleSource;
  }

  public static boolean isLuaFile(String path) {
    return path.matches(".*\\.lua");
  }

  public void setVariable(String variableName, Object value) {
    sampleSource.setVariable(variableName, value);
  }

  public void resetStep() {
    sampleSource.resetStep();
  }
}
