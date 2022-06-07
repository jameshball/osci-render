package sh.ball.parser.lua;

import sh.ball.audio.FrameSource;
import sh.ball.parser.FileParser;
import sh.ball.shapes.Vector2;

import javax.script.*;
import java.io.*;
import java.util.stream.Collectors;

public class LuaParser extends FileParser<FrameSource<Vector2>> {

  private final LuaSampleSource sampleSource = new LuaSampleSource();

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
    ScriptEngineManager sem = new ScriptEngineManager();
    Compilable e = (Compilable) sem.getEngineByName("luaj");
    CompiledScript compiledScript = e.compile(script);

    sampleSource.setScript(script, compiledScript);

    return sampleSource;
  }

  public static boolean isLuaFile(String path) {
    return path.matches(".*\\.lua");
  }
}
