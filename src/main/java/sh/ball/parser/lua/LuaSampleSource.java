package sh.ball.parser.lua;

import org.luaj.vm2.LuaValue;
import sh.ball.audio.FrameSource;
import sh.ball.shapes.Vector2;

import javax.script.Bindings;
import javax.script.CompiledScript;
import javax.script.SimpleBindings;

public class LuaSampleSource implements FrameSource<Vector2> {

  private final CompiledScript script;
  private final Bindings bindings = new SimpleBindings();

  private boolean active = true;
  private long step = 1;
  private double theta = 0.0;

  public LuaSampleSource(CompiledScript script) {
    this.script = script;
  }

  @Override
  public Vector2 next() {
    try {
      bindings.put("step", (double) step);
      bindings.put("theta", theta);
      LuaValue result = (LuaValue) script.eval(bindings);
      step++;
      theta = result.get(3).checkdouble();
      return new Vector2(result.get(1).checkdouble(), result.get(2).checkdouble());
    } catch (Exception e) {
      e.printStackTrace();
    }
    return new Vector2();
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
}
