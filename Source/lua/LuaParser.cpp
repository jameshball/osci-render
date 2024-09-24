#include "LuaParser.h"
#include "luaimport.h"

std::function<void(const std::string&)> LuaParser::onPrint;
std::function<void()> LuaParser::onClear;

void LuaParser::maximumInstructionsReached(lua_State* L, lua_Debug* D) {
    lua_getstack(L, 1, D);
    lua_getinfo(L, "l", D);
    
	std::string msg = std::to_string(D->currentline) + ": Maximum instructions reached! You may have an infinite loop.";
	lua_pushstring(L, msg.c_str());
	lua_error(L);
}

void LuaParser::setMaximumInstructions(lua_State*& L, int count) {
    lua_sethook(L, LuaParser::maximumInstructionsReached, LUA_MASKCOUNT, count);
}

void LuaParser::resetMaximumInstructions(lua_State*& L) {
    lua_sethook(L, LuaParser::maximumInstructionsReached, 0, 0);
}

static int luaPrint(lua_State* L) {
    int nargs = lua_gettop(L);

    for (int i = 1; i <= nargs; ++i) {
        LuaParser::onPrint(luaL_tolstring(L, i, nullptr));
        lua_pop(L, 1);
    }

    return 0;
}

static int luaClear(lua_State* L) {
    LuaParser::onClear();
    return 0;
}

static const struct luaL_Reg luaLib[] = {
  {"print", luaPrint},
  {"clear", luaClear},
  {NULL, NULL} /* end of array */
};

extern int luaopen_customprintlib(lua_State* L) {
    lua_getglobal(L, "_G");
    luaL_setfuncs(L, luaLib, 0);
    lua_pop(L, 1);
    return 0;
}

LuaParser::LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript) : script(script), fallbackScript(fallbackScript), errorCallback(errorCallback), fileName(fileName) {}

void LuaParser::reset(lua_State*& L, juce::String script) {
    functionRef = -1;

    if (L != nullptr) {
        lua_close(L);
    }
    
    L = luaL_newstate();
    luaL_openlibs(L);
	luaopen_customprintlib(L);
    
    this->script = script;
    parse(L);
}

void LuaParser::reportError(const char* errorChars) {
    std::string error = errorChars;
    std::regex nilRegex = std::regex(R"(attempt to.*nil value.*'slider_\w')");
    // ignore nil errors about global variables, these are likely caused by other errors
    if (std::regex_search(error, nilRegex)) {
        return;
    }

    // remove any newlines from error message
    error = std::regex_replace(error, std::regex(R"(\n|\r)"), "");
    // remove script content from error message
    error = std::regex_replace(error, std::regex(R"(^\[string ".*"\]:)"), "");
    // extract line number from start of error message
    std::regex lineRegex(R"(^(\d+): )");
    std::smatch lineMatch;
    std::regex_search(error, lineMatch, lineRegex);

    if (lineMatch.size() > 1) {
        int line = std::stoi(lineMatch[1]);
        // remove line number from error message
        error = std::regex_replace(error, lineRegex, "");
        errorCallback(line, fileName, error);
    }
}

void LuaParser::parse(lua_State*& L) {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        reportError(error);
        lua_pop(L, 1);
        revertToFallback(L);
    } else {
        functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, double value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, int value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariables(lua_State*& L, LuaVariables& vars) {
	setGlobalVariable(L, "step", vars.step);
	setGlobalVariable(L, "sample_rate", vars.sampleRate);
	setGlobalVariable(L, "frequency", vars.frequency);
	setGlobalVariable(L, "phase", vars.phase);

    for (int i = 0; i < NUM_SLIDERS; i++) {
		setGlobalVariable(L, SLIDER_NAMES[i], vars.sliders[i]);
    }

    if (vars.isEffect) {
		setGlobalVariable(L, "x", vars.x);
		setGlobalVariable(L, "y", vars.y);
		setGlobalVariable(L, "z", vars.z);
    }
}

void LuaParser::incrementVars(LuaVariables& vars) {
    vars.step++;
    vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
    if (vars.phase > 2 * std::numbers::pi) {
        vars.phase -= 2 * std::numbers::pi;
    }
}

void LuaParser::clearStack(lua_State*& L) {
    lua_settop(L, 0);
}

void LuaParser::revertToFallback(lua_State*& L) {
    functionRef = -1;
    usingFallbackScript = true;
    if (script != fallbackScript) {
        reset(L, fallbackScript);
    }
}

void LuaParser::readTable(lua_State*& L, std::vector<float>& values) {
    auto length = lua_rawlen(L, -1);

    for (int i = 1; i <= length; i++) {
        lua_pushinteger(L, i);
        lua_gettable(L, -2);
        float value = lua_tonumber(L, -1);
        lua_pop(L, 1);
        values.push_back(value);
    }
}

// only the audio thread runs this fuction
std::vector<float> LuaParser::run(lua_State*& L, LuaVariables& vars) {
    // if we haven't seen this state before, reset it
    int stateIndex = std::find(seenStates.begin(), seenStates.end(), L) - seenStates.begin();
    if (stateIndex == seenStates.size()) {
        reset(L, script);
        seenStates.push_back(L);
    }

    std::vector<float> values;
	
	setGlobalVariables(L, vars);
    
	// Get the function from the registry
	lua_geti(L, LUA_REGISTRYINDEX, functionRef);

    setMaximumInstructions(L, 5000000);
    
    if (lua_isfunction(L, -1)) {
        const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (ret != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            reportError(error);
            revertToFallback(L);
        } else {            
            if (lua_istable(L, -1)) {
                readTable(L, values);
            }
        }
    } else {
        revertToFallback(L);
    }

    resetMaximumInstructions(L);

    if (functionRef != -1 && !usingFallbackScript) {
        resetErrors();
    }

	clearStack(L);
    
	incrementVars(vars);
    
	return values;
}

bool LuaParser::isFunctionValid() {
    return functionRef != -1;
}

juce::String LuaParser::getScript() {
    return script;
}

void LuaParser::resetErrors() {
    errorCallback(-1, fileName, "");
}

void LuaParser::close(lua_State*& L) {
    if (L != nullptr) {
        lua_close(L);
    }
}
