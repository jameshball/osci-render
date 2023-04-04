#include "LuaParser.h"
#include "luaimport.h"


LuaParser::LuaParser(juce::String script) {
    // initialization
    L = luaL_newstate();
    luaL_openlibs(L);

    this->script = script;
}

LuaParser::~LuaParser() {
    lua_close(L);
}

std::vector<std::unique_ptr<Shape>> LuaParser::draw() {
	std::vector<std::unique_ptr<Shape>> shapes;
	
    for (int i = 0; i < 100; i++) {
        lua_pushnumber(L, step);
        lua_setglobal(L, "step");
        
        const int ret = luaL_dostring(L, script.toUTF8());
		if (ret != 0) {
			const char* error = lua_tostring(L, -1);
			DBG(error);
			lua_pop(L, 1);
        } else if (lua_istable(L, -1)) {
            // get the first element of the table
            lua_pushinteger(L, 1);
            lua_gettable(L, -2);
            float x = (int)lua_tonumber(L, -1);
            lua_pop(L, 1);

            // get the second element of the table
            lua_pushinteger(L, 2);
            lua_gettable(L, -2);
            float y = (int)lua_tonumber(L, -1);
            lua_pop(L, 1);

            shapes.push_back(std::make_unique<Vector2>(x, y));
        }
        
		// pop the table
		lua_pop(L, 1);

		step++;
    }
    
	return shapes;
}
