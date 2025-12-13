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
    
    LuaEffectState(const juce::String& fileName, const juce::String& initialCode, std::function<void(int, juce::String, juce::String)> errorCallback)
        : code(initialCode), defaultScript(initialCode == "return { x, y, z }")
    {
        parser = std::make_unique<LuaParser>(fileName, code, errorCallback);
    }
    
    void updateCode(const juce::String& newCode) {
        juce::SpinLock::ScopedLockType lock(codeLock);
        defaultScript = (newCode == "return { x, y, z }");
        code = newCode;
        parser = std::make_unique<LuaParser>("Custom Lua Effect", code, parser->getErrorCallback());
    }
    
    juce::String getCode() const {
        juce::SpinLock::ScopedLockType lock(codeLock);
        return code;
    }
};
