#include "LuaParser.h"
#include "luaimport.h"


LuaParser::LuaParser(juce::String script) {
    // initialization
    L = luaL_newstate();
    luaL_openlibs(L);

    this->script = script;
	parse();
}

LuaParser::~LuaParser() {
    lua_close(L);
}

void LuaParser::parse() {
    const int ret = luaL_loadstring(L, script.toUTF8());
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        DBG(error);
        lua_pop(L, 1);
        functionRef = -1;
    } else {
        functionRef = luaL_ref(L, LUA_REGISTRYINDEX);
    }
}

// only the audio thread runs this fuction
Vector2 LuaParser::draw() {
	Vector2 sample;
    
	if (functionRef == -1) {
		return sample;
	}
	
    lua_pushnumber(L, step);
    lua_setglobal(L, "step");

    // this CANNOT run at the same time as setVariable
    if (updateVariables) {
        juce::SpinLock::ScopedTryLockType lock(variableLock);
        if (lock.isLocked()) {
            for (int i = 0; i < variableNames.size(); i++) {
                lua_pushnumber(L, variables[i]);
                lua_setglobal(L, variableNames[i].toUTF8());
                DBG("set " << variableNames[i] << " to " << variables[i]);
            }
            variableNames.clear();
            variables.clear();
            updateVariables = false;
		}
    }
    
	lua_geti(L, LUA_REGISTRYINDEX, functionRef);
    
    const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
    if (ret != 0) {
        const char* error = lua_tostring(L, -1);
        DBG(error);
		functionRef = -1;
    } else if (lua_istable(L, -1)) {
        // get the first element of the table
        lua_pushinteger(L, 1);
        lua_gettable(L, -2);
        float x = lua_tonumber(L, -1);
        lua_pop(L, 1);

        // get the second element of the table
        lua_pushinteger(L, 2);
        lua_gettable(L, -2);
        float y = lua_tonumber(L, -1);
        lua_pop(L, 1);

        sample = Vector2(x, y);
    }

    lua_pop(L, 1);

	step++;
    
	return sample;
}

// this CANNOT run at the same time as draw()
// many threads can run this function
void LuaParser::setVariable(juce::String variableName, double value) {
    juce::SpinLock::ScopedLockType lock(variableLock);
	variableNames.push_back(variableName);
	variables.push_back(value);
    updateVariables = true;
}
