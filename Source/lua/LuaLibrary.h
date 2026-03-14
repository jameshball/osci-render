#pragma once

struct lua_State;

// Opens the osci standard library into the given Lua state.
// Registers all osci_* functions, print, and clear into _G.
extern int luaopen_oscilibrary(lua_State* L);
