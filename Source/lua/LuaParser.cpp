#include "LuaParser.h"
#include "luaimport.h"


LuaParser::LuaParser(juce::String script, juce::String fallbackScript) : fallbackScript(fallbackScript) {
    reset(script);
}

LuaParser::~LuaParser() {
    lua_close(L);
}

void LuaParser::reset(juce::String script) {
    functionRef = -1;

    if (L != nullptr) {
        lua_close(L);
    }
    
    L = luaL_newstate();
    lua_atpanic(L, panic);
    luaL_openlibs(L);

    this->script = script;
    parse();
}

void LuaParser::parse() {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);

		std::string newError = std::regex_replace(error, std::regex(R"(^\[string ".*"\]:)"), "");
        
        DBG(newError);
        lua_pop(L, 1);
        functionRef = -1;
        if (script != fallbackScript) {
            reset(fallbackScript);
        }
    } else {
        functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

// only the audio thread runs this fuction
std::vector<float> LuaParser::run() {
    std::vector<float> values;
	
    lua_pushnumber(L, step);
    lua_setglobal(L, "step");

    // this CANNOT run at the same time as setVariable
    if (updateVariables) {
        juce::SpinLock::ScopedTryLockType lock(variableLock);
        if (lock.isLocked()) {
            for (int i = 0; i < variableNames.size(); i++) {
                lua_pushnumber(L, variables[i]);
                lua_setglobal(L, variableNames[i].toUTF8());
            }
            variableNames.clear();
            variables.clear();
            updateVariables = false;
		}
    }
    
	lua_geti(L, LUA_REGISTRYINDEX, functionRef);
    
    if (lua_isfunction(L, -1)) {
        const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (ret != 0) {
            const char* error = lua_tostring(L, -1);
            DBG(error);
            functionRef = -1;
            if (script != fallbackScript) {
                reset(fallbackScript);
            }
        } else if (lua_istable(L, -1)) {
            auto length = lua_rawlen(L, -1);

            for (int i = 1; i <= length; i++) {
                lua_pushinteger(L, i);
                lua_gettable(L, -2);
                float value = lua_tonumber(L, -1);
                lua_pop(L, 1);
                values.push_back(value);
            }
        }
    } else {
        functionRef = -1;
        if (script != fallbackScript) {
            reset(fallbackScript);
        }
    }

    // clear stack
    lua_settop(L, 0);

	step++;
    
	return values;
}

// this CANNOT run at the same time as run()
// many threads can run this function
void LuaParser::setVariable(juce::String variableName, double value) {
    juce::SpinLock::ScopedLockType lock(variableLock);
	variableNames.push_back(variableName);
	variables.push_back(value);
    updateVariables = true;
}

bool LuaParser::isFunctionValid() {
    return functionRef != -1;
}

juce::String LuaParser::getScript() {
    return script;
}


int LuaParser::panic(lua_State *L) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "error object is not a string";
    DBG("PANIC: unprotected error in call to Lua API (%s)\n" << msg);
    return 0;  /* return to Lua to abort */
}