package sh.ball.parser.lua;

import org.luaj.vm2.*;
import org.luaj.vm2.lib.ThreeArgFunction;
import org.luaj.vm2.lib.TwoArgFunction;
import org.luaj.vm2.lib.jse.CoerceJavaToLua;
import sh.ball.audio.FrameSource;
import sh.ball.shapes.Vector2;

import javax.script.Bindings;
import javax.script.CompiledScript;
import javax.script.ScriptException;
import javax.script.SimpleBindings;
import java.util.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class LuaSampleSource implements FrameSource<Vector2> {

  private final LuaExecutor executor;
  private boolean active = true;

  public LuaSampleSource(Globals globals) {
    this.executor = new LuaExecutor(globals);
  }

  public void setScript(String textScript) {
    executor.setScript(textScript);
  }

  @Override
  public Vector2 next() {
    LuaValue result = executor.execute();
    if (result.isnil()) {
      return new Vector2();
    }
    return new Vector2(result.get(1).checkdouble(), result.get(2).checkdouble());
  }

  @Override
  public boolean isActive() {
    return active;
  }

  @Override
  public void disable() {
    active = false;
  }

  @Override
  public void enable() {
    active = true;
  }

  @Override
  public void setFrameSettings(Object settings) {}

  @Override
  public Object getFrameSettings() {
    return null;
  }

  public void setVariable(String variableName, Object value) {
    executor.setVariable(variableName, value);
  }

  public void resetStep() {
    executor.resetStep();
  }
}
