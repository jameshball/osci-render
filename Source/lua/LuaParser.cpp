#include "LuaParser.h"
#include "LuaLibrary.h"

// If you haven't compiled LuaJIT yet, this will fail, and you'll get a ton of syntax errors in a few Lua-related files!
// On all platforms, this should be done automatically when you run the export.
// If not, use the luajit_win.bat or luajit_linux_macos.sh scripts in the git root from the dev environment.
#include <lua.hpp>
#include <cstring>

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

LuaParser::LuaParser(juce::String fileName, juce::String script, std::function<void(int, juce::String, juce::String)> errorCallback, juce::String fallbackScript) : script(script), fallbackScript(fallbackScript), errorCallback(errorCallback), fileName(fileName) {
    // Pre-allocate to avoid audio-thread heap allocation when voices first call run().
    // 32 covers 16 ShapeVoice + 16 CustomEffect lua_State pointers.
    seenStates.reserve(32);
}

void LuaParser::reset(lua_State*& L, juce::String script) {
    functionRef = -1;

    if (L != nullptr) {
        seenStates.erase(std::remove(seenStates.begin(), seenStates.end(), L), seenStates.end());
        if (lastSeenState == L) lastSeenState = nullptr;
        lua_close(L);
    }
    
    L = luaL_newstate();
    luaL_openlibs(L);
	luaopen_oscilibrary(L);
    
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
        detectUsedVariables(script);
    }
}

void LuaParser::detectUsedVariables(const juce::String& scriptText) {
    uint64_t mask = 0;
    auto text = scriptText.toRawUTF8();

    // If script uses dynamic global access or loads external code, conservatively enable all variables
    if (strstr(text, "_G") || strstr(text, "getfenv") || strstr(text, "rawget")
        || strstr(text, "require") || strstr(text, "dofile") || strstr(text, "loadfile")) {
        usedVarMask = ~uint64_t(0);
        return;
    }

    if (strstr(text, "step"))               mask |= (1ULL << LuaVar_step);
    if (strstr(text, "sample_rate"))        mask |= (1ULL << LuaVar_sampleRate);
    if (strstr(text, "frequency"))          mask |= (1ULL << LuaVar_frequency);
    if (strstr(text, "phase"))              mask |= (1ULL << LuaVar_phase);
    if (strstr(text, "cycle_count"))        mask |= (1ULL << LuaVar_cycleCount);

    for (int i = 0; i < NUM_SLIDERS; i++) {
        if (strstr(text, SLIDER_NAMES[i]))  mask |= (1ULL << (LuaVar_sliderFirst + i));
    }

    if (strstr(text, "x"))                  mask |= (1ULL << LuaVar_x);
    if (strstr(text, "y"))                  mask |= (1ULL << LuaVar_y);
    if (strstr(text, "z"))                  mask |= (1ULL << LuaVar_z);
    if (strstr(text, "ext_x"))              mask |= (1ULL << LuaVar_extX);
    if (strstr(text, "ext_y"))              mask |= (1ULL << LuaVar_extY);

    if (strstr(text, "midi_note"))          mask |= (1ULL << LuaVar_midiNote);
    if (strstr(text, "velocity"))           mask |= (1ULL << LuaVar_velocity);
    if (strstr(text, "voice_index"))        mask |= (1ULL << LuaVar_voiceIndex);
    if (strstr(text, "note_on"))            mask |= (1ULL << LuaVar_noteOn);

    if (strstr(text, "bpm"))                mask |= (1ULL << LuaVar_bpm);
    if (strstr(text, "play_time"))          mask |= (1ULL << LuaVar_playTime);
    if (strstr(text, "play_time_beats"))    mask |= (1ULL << LuaVar_playTimeBeats);
    if (strstr(text, "is_playing"))         mask |= (1ULL << LuaVar_isPlaying);
    if (strstr(text, "time_sig_num"))       mask |= (1ULL << LuaVar_timeSigNum);
    if (strstr(text, "time_sig_den"))       mask |= (1ULL << LuaVar_timeSigDen);

    if (strstr(text, "envelope"))           mask |= (1ULL << LuaVar_envelope);
    if (strstr(text, "envelope_stage"))     mask |= (1ULL << LuaVar_envelopeStage);

    usedVarMask = mask;
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, double value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, int value) {
    lua_pushnumber(L, value);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariable(lua_State*& L, const char* name, bool value) {
    lua_pushboolean(L, value ? 1 : 0);
    lua_setglobal(L, name);
}

void LuaParser::setGlobalVariables(lua_State*& L, LuaVariables& vars) {
    auto set = [&](LuaVarBit bit, const char* name, auto value) {
        if (usedVarMask & (1ULL << bit))
            setGlobalVariable(L, name, value);
    };

    set(LuaVar_step,       "step",        vars.step);
    set(LuaVar_sampleRate, "sample_rate", vars.sampleRate);
    set(LuaVar_frequency,  "frequency",   vars.frequency);
    set(LuaVar_phase,      "phase",       vars.phase);
    set(LuaVar_cycleCount, "cycle_count", vars.cycle);

    for (int i = 0; i < NUM_SLIDERS; i++)
        set((LuaVarBit)(LuaVar_sliderFirst + i), SLIDER_NAMES[i], vars.sliders[i]);

    if (vars.isEffect) {
        set(LuaVar_x, "x", vars.x);
        set(LuaVar_y, "y", vars.y);
        set(LuaVar_z, "z", vars.z);
    }

    set(LuaVar_extX, "ext_x", vars.ext_x);
    set(LuaVar_extY, "ext_y", vars.ext_y);

    // MIDI context
    set(LuaVar_midiNote,   "midi_note",   vars.midiNote);
    set(LuaVar_velocity,   "velocity",    vars.velocity);
    set(LuaVar_voiceIndex, "voice_index", vars.voiceIndex);
    set(LuaVar_noteOn,     "note_on",     vars.noteOn);

    // DAW transport
    set(LuaVar_bpm,           "bpm",             vars.bpm);
    set(LuaVar_playTime,      "play_time",        vars.playTime);
    set(LuaVar_playTimeBeats, "play_time_beats",  vars.playTimeBeats);
    set(LuaVar_isPlaying,     "is_playing",       vars.isPlaying);
    set(LuaVar_timeSigNum,    "time_sig_num",     vars.timeSigNumerator);
    set(LuaVar_timeSigDen,    "time_sig_den",     vars.timeSigDenominator);

    // Envelope
    set(LuaVar_envelope,      "envelope",       vars.envelope);
    set(LuaVar_envelopeStage, "envelope_stage", vars.envelopeStage);
}

void LuaParser::incrementVars(LuaVariables& vars) {
    vars.step++;
    vars.phase += 2 * std::numbers::pi * vars.frequency / vars.sampleRate;
    if (vars.phase > 2 * std::numbers::pi) {
        vars.phase -= 2 * std::numbers::pi;
        vars.cycle += 1;
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
        seenStates.push_back(L);
        lastSeenState = L;
    }
}

void LuaParser::readTable(lua_State*& L, LuaResult& result) {
    int length = std::min((int)lua_objlen(L, -1), MAX_LUA_RESULT_VALUES);
    result.count = length;

    for (int i = 1; i <= length; i++) {
        lua_rawgeti(L, -1, i);
        result.values[i - 1] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
}

// only the audio thread runs this fuction
LuaResult LuaParser::run(lua_State*& L, LuaVariables& vars) {
    // if we haven't seen this state before, reset it
    if (L == nullptr || L != lastSeenState) {
        if (std::find(seenStates.begin(), seenStates.end(), L) == seenStates.end()) {
            reset(L, script);
            seenStates.push_back(L);
        }
        lastSeenState = L;
    }

    LuaResult result;

    // Reset instruction counter before setting globals and calling pcall.
    setMaximumInstructions(L, 5000000);
	
	setGlobalVariables(L, vars);
    
	// Get the function from the registry
	lua_rawgeti(L, LUA_REGISTRYINDEX, functionRef);
    
    if (lua_isfunction(L, -1)) {
        const int ret = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (ret != LUA_OK) {
            const char* error = lua_tostring(L, -1);
            reportError(error);
            revertToFallback(L);
        } else {            
            if (lua_istable(L, -1)) {
                readTable(L, result);
            }
        }
    } else {
        revertToFallback(L);
    }

    if (functionRef != -1 && !usingFallbackScript) {
        resetErrors();
    }

	clearStack(L);
    
	incrementVars(vars);
    
	return result;
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
        seenStates.erase(std::remove(seenStates.begin(), seenStates.end(), L), seenStates.end());
        if (lastSeenState == L) lastSeenState = nullptr;
        lua_close(L);
    }
}
