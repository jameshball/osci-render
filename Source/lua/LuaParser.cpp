#include "LuaParser.h"
#include "luaimport.h"

LuaParser::LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, std::function<LuaVariables()> variableCallback, juce::String fallbackScript) : fallbackScript(fallbackScript), errorCallback(errorCallback), variableCallback(variableCallback), fileName(fileName) {
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

void LuaParser::parse() {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        reportError(error);
        lua_pop(L, 1);
        functionRef = -1;
        usingFallbackScript = true;
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

    auto vars = variableCallback();

    lua_pushnumber(L, vars.sampleRate);
    lua_setglobal(L, "sample_rate");

    lua_pushnumber(L, vars.frequency);
    lua_setglobal(L, "frequency");

    lua_pushnumber(L, phase);
    lua_setglobal(L, "phase");

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
        if (ret != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            reportError(error);
            functionRef = -1;
            usingFallbackScript = true;
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
        usingFallbackScript = true;
        if (script != fallbackScript) {
            reset(fallbackScript);
        }
    }

    if (functionRef != -1 && !usingFallbackScript) {
        resetErrors();
    }

    // clear stack
    lua_settop(L, 0);

	step++;
    phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
    if (phase > 2 * std::numbers::pi) {
        phase -= 2 * std::numbers::pi;
    }
    
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

void LuaParser::resetErrors() {
    errorCallback(-1, fileName, "");
}

int LuaParser::panic(lua_State *L) {
    const char *msg = lua_tostring(L, -1);
    if (msg == NULL) msg = "error object is not a string";
    DBG("PANIC: unprotected error in call to Lua API (%s)\n" << msg);
    return 0;  /* return to Lua to abort */
}