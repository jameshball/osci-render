#pragma once
#include <JuceHeader.h>
#include "../lua/LuaParser.h"

// Global holder for compiled Lua code (shared across all voices)
class LuaEffectState {
public:
    std::unique_ptr<LuaParser> parser;
    juce::SpinLock codeLock;
    juce::String code;
    bool defaultScript = true;

    inline static const juce::String UNIQUE_ID = "6a3580b0-c5fc-4b28-a33e-e26a487f052f";
    inline static juce::String FILE_NAME = "Custom Lua Effect";
    
    LuaEffectState(const juce::String& fileName, const juce::String& initialCode, std::function<void(int, juce::String, juce::String)> errorCallback)
        : code(initialCode), defaultScript(initialCode == "return { x, y, z }")
    {
        parser = std::make_unique<LuaParser>(fileName, code, errorCallback);
    }
    
    void updateCode(const juce::String& newCode) {
        juce::SpinLock::ScopedLockType lock(codeLock);
        defaultScript = (newCode == "return { x, y, z }");
        code = newCode;
        parser = std::make_unique<LuaParser>(LuaEffectState::UNIQUE_ID, code, parser->getErrorCallback());
    }
    
    juce::String getCode() const {
        juce::SpinLock::ScopedLockType lock(codeLock);
        return code;
    }
};
