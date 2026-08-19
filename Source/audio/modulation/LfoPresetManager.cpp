#include "LfoPresetManager.h"

LfoPresetManager::LfoPresetManager(const juce::File& presetsDirectory)
    : presetsDir(presetsDirectory) {
    presetsDir.createDirectory();
}

std::vector<LfoPresetManager::PresetEntry> LfoPresetManager::getUserPresets() const {
    std::vector<PresetEntry> presets;

    for (const auto& file : presetsDir.findChildFiles(juce::File::findFiles, false, "*.vitallfo")) {
        PresetEntry entry;
        entry.file = file;

        auto text = file.loadFileAsString();
        auto parsed = juce::JSON::parse(text);
        if (parsed.isObject()) {
            auto* obj = parsed.getDynamicObject();
            if (obj && obj->hasProperty("name"))
                entry.name = obj->getProperty("name").toString();
        }

        if (entry.name.isEmpty())
            entry.name = file.getFileNameWithoutExtension();

        presets.push_back(entry);
    }

    std::sort(presets.begin(), presets.end(),
              [](const PresetEntry& a, const PresetEntry& b) {
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    return presets;
}

bool LfoPresetManager::savePreset(const juce::String& name, const LfoWaveform& waveform) {
    auto json = waveformToVitalJson(waveform, name);
    auto text = juce::JSON::toString(json);
    auto file = presetsDir.getChildFile(sanitizeFilename(name) + ".vitallfo");
    return file.replaceWithText(text);
}

bool LfoPresetManager::loadPreset(const juce::File& file, LfoWaveform& waveform, juce::String& name) {
    auto text = file.loadFileAsString();
    if (text.isEmpty()) return false;

    auto parsed = juce::JSON::parse(text);
    return vitalJsonToWaveform(parsed, waveform, name);
}

bool LfoPresetManager::deletePreset(const juce::File& file) {
    if (!file.existsAsFile()) return false;
    if (!file.isAChildOf(presetsDir)) return false;
    return file.deleteFile();
}

juce::File LfoPresetManager::getVitalBaseDirectory() {
#if JUCE_MAC
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Music/Vital");
#elif JUCE_WINDOWS
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("Vital");
#else
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile(".local/share/vital");
#endif
}

const std::vector<LfoPresetManager::PresetEntry>& LfoPresetManager::getVitalUserPresets() const {
    if (vitalCacheValid)
        return vitalPresetsCache;

    vitalPresetsCache.clear();

    auto vitalBase = getVitalBaseDirectory();
    if (!vitalBase.isDirectory()) {
        vitalCacheValid = true;
        return vitalPresetsCache;
    }

    // Scan all Vital/*/LFOs directories, excluding Factory
    for (const auto& subDir : vitalBase.findChildFiles(juce::File::findDirectories, false)) {
        if (subDir.getFileName().equalsIgnoreCase("Factory"))
            continue;

        auto lfosDir = subDir.getChildFile("LFOs");
        if (!lfosDir.isDirectory())
            continue;

        juce::String folderName = subDir.getFileName();

        for (const auto& file : lfosDir.findChildFiles(juce::File::findFiles, false, "*.vitallfo")) {
            PresetEntry entry;
            entry.file = file;
            entry.category = folderName;

            auto text = file.loadFileAsString();
            auto parsed = juce::JSON::parse(text);
            if (parsed.isObject()) {
                auto* obj = parsed.getDynamicObject();
                if (obj && obj->hasProperty("name"))
                    entry.name = obj->getProperty("name").toString();
            }

            if (entry.name.isEmpty())
                entry.name = file.getFileNameWithoutExtension();

            vitalPresetsCache.push_back(entry);
        }
    }

    std::sort(vitalPresetsCache.begin(), vitalPresetsCache.end(),
              [](const PresetEntry& a, const PresetEntry& b) {
                  int catCmp = a.category.compareIgnoreCase(b.category);
                  if (catCmp != 0) return catCmp < 0;
                  return a.name.compareIgnoreCase(b.name) < 0;
              });

    vitalCacheValid = true;
    return vitalPresetsCache;
}

juce::var LfoPresetManager::waveformToVitalJson(const LfoWaveform& waveform, const juce::String& name) {
    auto* root = new juce::DynamicObject();
    root->setProperty("name", name);
    root->setProperty("num_points", (int)waveform.nodes.size());
    root->setProperty("smooth", waveform.smooth);

    juce::Array<juce::var> points;
    juce::Array<juce::var> powers;

    for (size_t i = 0; i < waveform.nodes.size(); ++i) {
        const auto& node = waveform.nodes[i];
        points.add(node.time);
        points.add(1.0 - node.value);  // Invert y-axis for Vital format
    }

    // powers[i] shapes the segment from point[i] to point[i+1].
    // In our format, node[j].curve shapes the segment ending at node[j] (from j-1 to j).
    // Mapping: powers[i] = node[i+1].curve  for i = 0..N-2
    //          powers[N-1] = 0.0  (wrap segment, unused)
    for (size_t i = 0; i < waveform.nodes.size(); ++i) {
        if (i + 1 < waveform.nodes.size())
            powers.add((double)waveform.nodes[i + 1].curve);
        else
            powers.add(0.0);
    }

    root->setProperty("points", points);
    root->setProperty("powers", powers);

    return juce::var(root);
}

bool LfoPresetManager::vitalJsonToWaveform(const juce::var& json, LfoWaveform& waveform, juce::String& name) {
    if (!json.isObject()) return false;

    auto* obj = json.getDynamicObject();
    if (!obj) return false;

    name = obj->getProperty("name").toString();
    int numPoints = (int)obj->getProperty("num_points");
    bool smooth = (bool)obj->getProperty("smooth");

    auto pointsVar = obj->getProperty("points");
    if (!pointsVar.isArray()) return false;
    auto* pointsArray = pointsVar.getArray();
    if (!pointsArray || (int)pointsArray->size() < numPoints * 2) return false;

    auto powersVar = obj->getProperty("powers");
    juce::Array<juce::var>* powersArray = nullptr;
    if (powersVar.isArray())
        powersArray = powersVar.getArray();

    if (numPoints < 2 || numPoints > 1000) return false;

    waveform.nodes.clear();
    waveform.smooth = smooth;

    for (int i = 0; i < numPoints; ++i) {
        LfoNode node;
        double rawTime = (double)(*pointsArray)[i * 2];
        double rawValue = (double)(*pointsArray)[i * 2 + 1];

        if (std::isnan(rawTime) || std::isinf(rawTime) ||
            std::isnan(rawValue) || std::isinf(rawValue))
            return false;

        node.time = juce::jlimit(0.0, 1.0, rawTime);
        node.value = juce::jlimit(0.0, 1.0, 1.0 - rawValue);  // Invert y

        // powers[i-1] shapes the segment from point[i-1] to point[i], stored on node[i].curve
        if (i > 0 && powersArray && i - 1 < (int)powersArray->size()) {
            float power = (float)(double)(*powersArray)[i - 1];
            if (std::isnan(power) || std::isinf(power))
                power = 0.0f;
            node.curve = juce::jlimit(-osci_audio::kMaxPower, osci_audio::kMaxPower, power);
        } else {
            node.curve = 0.0f;
        }

        waveform.nodes.push_back(node);
    }

    return true;
}

juce::String LfoPresetManager::sanitizeFilename(const juce::String& name) {
    juce::String sanitized;
    for (auto ch : name) {
        if (juce::CharacterFunctions::isLetterOrDigit(ch) || ch == ' ' || ch == '-' || ch == '_')
            sanitized += ch;
    }
    return sanitized.trim().isEmpty() ? "Untitled" : sanitized.trim();
}
