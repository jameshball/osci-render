#pragma once

#include "../parser/FileParser.h"
#include <array>

// Host parameters have a fixed inventory. Scene entities keep their own parameter
// values until explicitly connected to one of these project-wide slots.
namespace scene {
constexpr int slotCount = 8;
constexpr int controlCount = 9;
constexpr int voiceCount = 32;
inline const std::array<const char*, controlCount> controlNames {
    "Position X", "Position Y", "Position Z", "Rotation X", "Rotation Y", "Rotation Z", "Scale X", "Scale Y", "Scale Z"
};
inline float defaultValue(int i) { return i >= 6 ? 1.0f : 0.0f; }
inline float minimum(int i) { return i >= 6 ? 0.01f : (i >= 3 ? -180.0f : -20.0f); }
inline float maximum(int i) { return i >= 6 ? 20.0f : (i >= 3 ? 180.0f : 20.0f); }

struct Automation {
    std::array<std::array<osci::FloatParameter*, controlCount>, slotCount> parameters {};
    std::array<juce::String, slotCount> targets;
    std::array<std::atomic<unsigned>, slotCount> generations {};

    void initialise(std::vector<osci::FloatParameter*>& destination) {
        for (int s = 0; s < slotCount; ++s) {
            for (int i = 0; i < controlCount; ++i) {
                auto* p = new osci::FloatParameter("Scene Slot " + juce::String(s + 1) + " " + controlNames[i],
                    "sceneSlot" + juce::String(s + 1) + "Control" + juce::String(i), 2, defaultValue(i), minimum(i), maximum(i), 0.001f);
                parameters[s][i] = p;
                destination.push_back(p);
            }
        }
    }
};

struct Transform {
    explicit Transform(Automation& a) : automation(a), id(juce::Uuid().toString()) {
        for (int i = 0; i < controlCount; ++i) {
            values[i] = std::make_unique<osci::FloatParameter>(controlNames[i], id + juce::String(i), 2,
                defaultValue(i), minimum(i), maximum(i), 0.001f);
        }
    }

    int getSlot() const {
        const int s = slot.load();
        return s >= 0 && automation.generations[s].load() == bindingGeneration ? s : -1;
    }
    osci::FloatParameter& parameter(int i) const {
        const auto s = getSlot();
        return s >= 0 ? *automation.parameters[s][i] : *values[i];
    }
    float get(int i) const { return parameter(i).getValueUnnormalised(); }
    void set(int i, float value) { parameter(i).setUnnormalisedValueNotifyingHost(value); }
    bool expose() {
        if (getSlot() >= 0) {
            return true;
        }
        for (int s = 0; s < slotCount; ++s) {
            if (automation.targets[s].isEmpty()) {
                for (int i = 0; i < controlCount; ++i) {
                    automation.parameters[s][i]->setUnnormalisedValueNotifyingHost(get(i));
                }
                automation.targets[s] = id;
                bindingGeneration = automation.generations[s].load();
                slot.store(s);
                return true;
            }
        }
        return false;
    }
    void unexpose() {
        const int s = getSlot();
        if (s < 0) {
            return;
        }
        for (int i = 0; i < controlCount; ++i) {
            values[i]->setValueUnnormalised(get(i));
        }
        slot.store(-1);
        automation.targets[s].clear();
    }
    void save(juce::XmlElement& xml) const {
        xml.setAttribute("id", id);
        xml.setAttribute("slot", getSlot());
        for (int i = 0; i < controlCount; ++i) {
            xml.setAttribute("v" + juce::String(i), get(i));
        }
    }
    void load(const juce::XmlElement& xml, bool copy = false) {
        if (!copy) {
            id = xml.getStringAttribute("id", id);
        }
        for (int i = 0; i < controlCount; ++i) {
            values[i]->setValueUnnormalised(juce::jlimit(minimum(i), maximum(i), (float)xml.getDoubleAttribute("v" + juce::String(i), defaultValue(i))));
        }
        const int s = copy ? -1 : xml.getIntAttribute("slot", -1);
        if (s >= 0 && s < slotCount && (automation.targets[s].isEmpty() || automation.targets[s] == id)) {
            automation.targets[s] = id;
            bindingGeneration = automation.generations[s].load();
            slot.store(s);
        }
    }
    osci::Point apply(osci::Point p) const {
        p.scale(get(6), get(7), get(8));
        p.rotate(juce::degreesToRadians(get(3)), juce::degreesToRadians(get(4)), juce::degreesToRadians(get(5)));
        p.translate(get(0), get(1), get(2));
        return p;
    }
    // Camera coordinates are a target, Euler orbit, and view scale. Identity
    // framing preserves the source's existing normalised coordinates.
    osci::Point view(osci::Point p) const {
        p.translate(-get(0), -get(1), -get(2));
        p.rotate(0, 0, -juce::degreesToRadians(get(5)));
        p.rotate(0, -juce::degreesToRadians(get(4)), 0);
        p.rotate(-juce::degreesToRadians(get(3)), 0, 0);
        p.scale(1.0f / get(6), 1.0f / get(6), 1.0f / get(6));
        return p;
    }

    Automation& automation;
    juce::String id;
    std::atomic<int> slot {-1};
    std::atomic<unsigned> bindingGeneration {0};
    std::array<std::unique_ptr<osci::FloatParameter>, controlCount> values;
};

struct Object {
    Object(Automation& bank, juce::String n, std::shared_ptr<juce::MemoryBlock> d, std::shared_ptr<FileParser> p)
        : transform(bank), name(std::move(n)), data(std::move(d)), parser(std::move(p)) {}
    Transform transform;
    juce::String name;
    std::shared_ptr<juce::MemoryBlock> data;
    std::shared_ptr<FileParser> parser;
    juce::SpinLock geometryLock;
    std::vector<std::unique_ptr<osci::Shape>> geometry;
    std::vector<double> ends;
    struct PreviewPoint { osci::Point point; bool start; };
    std::vector<PreviewPoint> preview;
    double totalLength = 0;
    std::array<LuaState, voiceCount> lua;
    std::array<LuaVariables, voiceCount> variables;
    // A bounded trace collected from actual source output makes procedural and
    // live sample sources visible without executing them a second time in the UI.
    std::array<osci::Point, 1024> trace;
    int traceWrite = 0;
    int traceSize = 0;
    std::shared_ptr<Object> liveSource;
    bool liveGeometry = false;
    juce::String liveKind;

    void replaceGeometry(std::vector<std::unique_ptr<osci::Shape>> prepared) {
        std::vector<double> preparedEnds;
        preparedEnds.reserve(prepared.size());
        double length = 0;
        for (auto& shape : prepared) {
            length += juce::jmax(0.000001, (double)shape->length());
            preparedEnds.push_back(length);
        }
        std::vector<PreviewPoint> preparedPreview;
        const size_t stride = juce::jmax((size_t)1, (prepared.size() + 1023) / 1024);
        for (size_t i = 0; i < prepared.size(); i += stride) {
            for (int j = 0; j <= 4; ++j) {
                preparedPreview.push_back({ prepared[i]->nextVector(j / 4.0f), j == 0 });
            }
        }
        {
            juce::SpinLock::ScopedLockType guard(geometryLock);
            preview.swap(preparedPreview);
            geometry.swap(prepared);
            ends.swap(preparedEnds);
            totalLength = length;
        }
        // Old geometry is reclaimed by the producer, never the audio thread.
    }
    osci::Point pointAt(double phase) {
        if (liveSource != nullptr) { return liveSource->pointAt(phase); }
        juce::SpinLock::ScopedLockType guard(geometryLock);
        if (geometry.empty()) {
            return {};
        }
        const double distance = phase * totalLength;
        const auto found = std::lower_bound(ends.begin(), ends.end(), distance);
        const auto i = juce::jmin(geometry.size() - 1, (size_t)std::distance(ends.begin(), found));
        const double start = i == 0 ? 0 : ends[i - 1];
        return geometry[i]->nextVector((float)juce::jlimit(0.0, 1.0, (distance - start) / (ends[i] - start)));
    }
};

class Scene : private juce::Thread {
public:
    explicit Scene(Automation& a) : juce::Thread("Scene geometry"), camera(a), automation(a) { startThread(); }
    ~Scene() override { stopThread(-1); }
    Transform camera;
    Automation& automation;
    juce::SpinLock lock;
    std::vector<std::shared_ptr<Object>> objects;
    std::atomic<bool> preparing {false};

    osci::Point render(double phase, LuaVariables& context, int voice) {
        juce::SpinLock::ScopedLockType guard(lock);
        if (objects.empty()) {
            return {};
        }
        const double position = phase * objects.size();
        const auto index = juce::jmin(objects.size() - 1, (size_t)position);
        osci::Point point;
        for (size_t i = 0; i < objects.size(); ++i) {
            auto& object = *objects[i];
            if (object.parser != nullptr && object.parser->isSample()) {
                const int v = juce::jlimit(0, voiceCount - 1, voice);
                auto& vars = object.variables[v];
                const auto step = vars.step;
                const auto cycle = vars.cycle;
                vars = context;
                vars.step = step;
                vars.cycle = cycle;
                vars.phase = (position - std::floor(position)) * juce::MathConstants<double>::twoPi;
                vars.frequency *= objects.size();
                const auto sample = object.parser->nextSample(object.lua[v], vars);
                if (i == index) {
                    point = sample;
                }
                object.trace[object.traceWrite] = sample;
                object.traceWrite = (object.traceWrite + 1) % (int)object.trace.size();
                object.traceSize = juce::jmin(object.traceSize + 1, (int)object.trace.size());
            }
        }
        auto& object = *objects[index];
        if (object.parser == nullptr || !object.parser->isSample()) {
            point = object.pointAt(position - index);
        }
        return camera.view(object.transform.apply(point));
    }

    double frameRate() {
        juce::SpinLock::ScopedLockType guard(lock);
        for (const auto& object : objects) {
            if (object->parser != nullptr && object->parser->isAnimatable) {
                return juce::jmax(1.0, object->parser->getFrameRate());
            }
        }
        return 30.0;
    }
    int numFrames() {
        const double rate = frameRate();
        juce::SpinLock::ScopedLockType guard(lock);
        double duration = 0;
        for (const auto& object : objects) {
            if (object->parser != nullptr && object->parser->isAnimatable) {
                duration = juce::jmax(duration, object->parser->getNumFrames() / juce::jmax(1.0, object->parser->getFrameRate()));
            }
        }
        return juce::jmax(1, (int)std::ceil(duration * rate));
    }
    void setFrame(double frame, bool loop) {
        const double seconds = frame / frameRate();
        juce::SpinLock::ScopedLockType guard(lock);
        for (const auto& object : objects) {
            auto* parser = object->parser.get();
            if (parser != nullptr && parser->isAnimatable) {
                const int count = juce::jmax(1, parser->getNumFrames());
                double position = seconds * parser->getFrameRate();
                if (loop) {
                    position = std::fmod(position, (double)count);
                    if (position < 0) { position += count; }
                }
                parser->setFrame((int)juce::jlimit(0.0, (double)count - 1, position));
            }
        }
    }

    void run() override {
        while (!threadShouldExit()) {
            if (!preparing.load()) { wait(30); continue; }
            std::vector<std::shared_ptr<Object>> sources;
            {
                juce::SpinLock::ScopedLockType guard(lock);
                sources = objects;
            }
            for (auto& object : sources) {
                if (object->parser != nullptr && !object->parser->isSample() && !object->liveGeometry) {
                    object->replaceGeometry(object->parser->nextFrame());
                }
            }
            wait(16);
        }
    }
};
}
