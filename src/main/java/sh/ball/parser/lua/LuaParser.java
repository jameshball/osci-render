package sh.ball.parser.lua;

import org.luaj.vm2.Globals;
import org.luaj.vm2.LuaFunction;
import org.luaj.vm2.lib.jse.JsePlatform;
import sh.ball.audio.FrameSource;
import sh.ball.parser.FileParser;
import sh.ball.shapes.Vector2;

import javax.script.*;
import java.io.*;
import java.util.stream.Collectors;

public class LuaParser extends FileParser<FrameSource<Vector2>> {

  private static final Globals globals = JsePlatform.standardGlobals();

  static {
    org.luaj.vm2.luajc.LuaJC.install(globals);
  }

  private final LuaSampleSource sampleSource = new LuaSampleSource(globals);

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
    LuaFunction f = globals.load(new StringReader(script), "script").checkfunction();

    sampleSource.setFunction(script, f);

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
