package sh.ball.parser.lua;

import org.luaj.vm2.*;
import org.luaj.vm2.lib.ThreeArgFunction;
import org.luaj.vm2.lib.TwoArgFunction;
import org.luaj.vm2.lib.jse.CoerceJavaToLua;
import org.luaj.vm2.lib.jse.JsePlatform;

import javax.script.ScriptException;
import java.io.StringReader;
import java.util.*;
import java.util.logging.Level;

import static sh.ball.gui.Gui.logger;

public class LuaExecutor {

  public static final Globals STANDARD_GLOBALS = JsePlatform.standardGlobals();

  static {
    org.luaj.vm2.luajc.LuaJC.install(STANDARD_GLOBALS);
  }

  private final Globals globals;
  private final Map<LuaString, LuaValue> bindings = new HashMap<>();
  private final Set<LuaString> programDefinedVariables = new HashSet<>();
  private LuaFunction lastWorkingFunction;
  private String lastWorkingTextScript;
  private LuaFunction function;
  private String textScript;
  private long step = 1;

  public LuaExecutor(Globals globals) {
    this.globals = globals;
  }

  public void setScript(String script) {
    LuaFunction f = LuaExecutor.STANDARD_GLOBALS.load(new StringReader(script), "script").checkfunction();
    setFunction(script, f);
  }

  private void setFunction(String textScript, LuaFunction function) {
    if (lastWorkingFunction == null) {
      lastWorkingFunction = function;
      lastWorkingTextScript = textScript;
    }
    this.function = function;
    this.textScript = textScript;
  }

  public LuaValue execute() {
    try {
      boolean updatedFile = false;

      globals.setmetatable(new BindingsMetatable(bindings));
      bindings.put(LuaValue.valueOf("step"), LuaValue.valueOf((double) step));

      LuaValue result;
      try {
        result = (LuaValue) LuaExecutor.eval(function, globals);
        if (!lastWorkingTextScript.equals(textScript)) {
          lastWorkingFunction = function;
          lastWorkingTextScript = textScript;
          updatedFile = true;
        }
      } catch (Exception e) {
        logger.log(Level.INFO, e.getMessage(), e);
        result = (LuaValue) LuaExecutor.eval(lastWorkingFunction, globals);
      }
      step++;

      if (updatedFile) {
        // reset variables
        step = 1;
        List<LuaString> variablesToRemove = new ArrayList<>();
        bindings.keySet().forEach(name -> {
          if (!programDefinedVariables.contains(name)) {
            variablesToRemove.add(name);
          }
        });
        variablesToRemove.forEach(bindings::remove);
      }

      return result;
    } catch (Exception e) {
      logger.log(Level.INFO, e.getMessage(), e);
    }

    return LuaValue.NIL;
  }

  public void setVariable(String variableName, Object value) {
    LuaString name = LuaValue.valueOf(variableName);
    programDefinedVariables.add(name);
    bindings.put(name, toLua(value));
  }

  public void resetStep() {
    step = 1;
  }

  /*
MIT License

Copyright (c) 2007 LuaJ. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit p ersons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

------------------------------

The below functions have all been modified by James Ball to optimise Lua parsing
on machines with poorer performance.


   */

  static class BindingsMetatable extends LuaTable {

    BindingsMetatable(Map<LuaString, LuaValue> bindings) {
      this.rawset(LuaValue.INDEX, new TwoArgFunction() {
        public LuaValue call(LuaValue table, LuaValue key) {
          LuaValue value = bindings.get(key.checkstring());
          if (value == null) {
            return LuaValue.NIL;
          } else {
            return value;
          }
        }
      });
      this.rawset(LuaValue.NEWINDEX, new ThreeArgFunction() {
        public LuaValue call(LuaValue table, LuaValue key, LuaValue value) {
          if (value == null || value.type() == LuaValue.TNIL) {
            bindings.remove(key.checkstring());
          } else {
            bindings.put(key.checkstring(), value);
          }
          return LuaValue.NONE;
        }
      });
    }
  }

  static private LuaValue toLua(Object javaValue) {
    return javaValue == null? LuaValue.NIL:
      javaValue instanceof LuaValue? (LuaValue) javaValue:
        CoerceJavaToLua.coerce(javaValue);
  }

  static private Object toJava(LuaValue luajValue) {
    return switch (luajValue.type()) {
      case LuaValue.TNIL -> null;
      case LuaValue.TSTRING -> luajValue.tojstring();
      case LuaValue.TUSERDATA -> luajValue.checkuserdata(Object.class);
      case LuaValue.TNUMBER -> luajValue.isinttype() ?
        (Object) luajValue.toint() :
        (Object) luajValue.todouble();
      default -> luajValue;
    };
  }

  static private Object toJava(Varargs v) {
    final int n = v.narg();
    switch (n) {
      case 0: return null;
      case 1: return toJava(v.arg1());
      default:
        Object[] o = new Object[n];
        for (int i=0; i<n; ++i)
          o[i] = toJava(v.arg(i+1));
        return o;
    }
  }

  static private Object eval(LuaFunction f, Globals g) throws ScriptException {
    try {
      f = f.getClass().newInstance();
    } catch (Exception e) {
      logger.log(Level.SEVERE, e.getMessage(), e);
      throw new ScriptException(e);
    }
    f.initupvalue1(g);
    return toJava(f.invoke(LuaValue.NONE));
  }
}
