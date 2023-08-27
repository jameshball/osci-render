#include "PluginProcessor.h"

void OscirenderAudioProcessor::openLegacyProject(const juce::XmlElement* xml) {
    juce::SpinLock::ScopedLockType lock1(parsersLock);
    juce::SpinLock::ScopedLockType lock2(effectsLock);

    if (xml != nullptr && xml->hasTagName("project")) {
        auto slidersXml = xml->getChildByName("sliders");
        if (slidersXml != nullptr) {
            for (auto sliderXml : slidersXml->getChildIterator()) {
                auto id = sliderXml->getTagName();
                auto valueXml = sliderXml->getChildByName("value");
                auto minXml = sliderXml->getChildByName("min");
                auto maxXml = sliderXml->getChildByName("max");

                double value = valueXml != nullptr ? valueXml->getAllSubText().getDoubleValue() : 0.0;
                double min = minXml != nullptr ? minXml->getAllSubText().getDoubleValue() : 0.0;
                double max = maxXml != nullptr ? maxXml->getAllSubText().getDoubleValue() : 0.0;

                value = valueFromLegacy(value, id);
                min = valueFromLegacy(min, id);
                max = valueFromLegacy(max, id);

                auto pair = effectFromLegacyId(id, true);
                auto effect = pair.first;
                auto parameter = pair.second;

                if (effect != nullptr && parameter != nullptr) {
                    if (id != "volume" && id != "threshold") {
                        parameter->min = min;
                        parameter->max = max;
                    }
                    parameter->setUnnormalisedValueNotifyingHost(value);
                }
            }
        }

        auto translationXml = xml->getChildByName("translation");
        if (translationXml != nullptr) {
            auto xXml = translationXml->getChildByName("x");
            auto yXml = translationXml->getChildByName("y");
            auto translateEffect = getEffect("translateX");
            if (translateEffect != nullptr) {
                auto x = translateEffect->getParameter("translateX");
                if (x != nullptr && xXml != nullptr) {
                    x->setUnnormalisedValueNotifyingHost(xXml->getAllSubText().getDoubleValue());
                }
                auto y = translateEffect->getParameter("translateY");
                if (y != nullptr && yXml != nullptr) {
                    y->setUnnormalisedValueNotifyingHost(yXml->getAllSubText().getDoubleValue());
                }
            }
        }

        auto perspectiveFixedRotateXml = xml->getChildByName("perspectiveFixedRotate");
        if (perspectiveFixedRotateXml != nullptr) {
            auto xXml = perspectiveFixedRotateXml->getChildByName("x");
            auto yXml = perspectiveFixedRotateXml->getChildByName("y");
            auto zXml = perspectiveFixedRotateXml->getChildByName("z");
            auto x = getBooleanParameter("perspectiveFixedRotateX");
            auto y = getBooleanParameter("perspectiveFixedRotateY");
            auto z = getBooleanParameter("perspectiveFixedRotateZ");
            if (x != nullptr && xXml != nullptr) {
                x->setBoolValueNotifyingHost(xXml->getAllSubText() == "true");
            }
            if (y != nullptr && yXml != nullptr) {
                y->setBoolValueNotifyingHost(yXml->getAllSubText() == "true");
            }
            if (z != nullptr && zXml != nullptr) {
                z->setBoolValueNotifyingHost(zXml->getAllSubText() == "true");
            }
        }

        auto objectFixedRotate = xml->getChildByName("objectFixedRotate");
        if (objectFixedRotate != nullptr) {
            auto xXml = objectFixedRotate->getChildByName("x");
            auto yXml = objectFixedRotate->getChildByName("y");
            auto zXml = objectFixedRotate->getChildByName("z");
            auto x = getBooleanParameter("objFixedRotateX");
            auto y = getBooleanParameter("objFixedRotateY");
            auto z = getBooleanParameter("objFixedRotateZ");
            if (x != nullptr && xXml != nullptr) {
                x->setBoolValueNotifyingHost(xXml->getAllSubText() == "true");
            }
            if (y != nullptr && yXml != nullptr) {
                y->setBoolValueNotifyingHost(yXml->getAllSubText() == "true");
            }
            if (z != nullptr && zXml != nullptr) {
                z->setBoolValueNotifyingHost(zXml->getAllSubText() == "true");
            }
        }

        auto checkBoxesXml = xml->getChildByName("checkBoxes");
        if (checkBoxesXml != nullptr) {
            for (auto checkBoxXml : checkBoxesXml->getChildIterator()) {
                auto id = checkBoxXml->getTagName();
                auto selectedXml = checkBoxXml->getChildByName("selected");
                auto animationXml = checkBoxXml->getChildByName("animation");
                auto animationSpeedXml = checkBoxXml->getChildByName("animationSpeed");

                bool selected = selectedXml != nullptr ? selectedXml->getAllSubText() == "true" : false;
                LfoType lfoType = animationXml != nullptr ? lfoTypeFromLegacyAnimationType(animationXml->getAllSubText()) : LfoType::Static;
                double lfoRate = animationSpeedXml != nullptr ? animationSpeedXml->getAllSubText().getDoubleValue() : 1.0;

                auto pair = effectFromLegacyId(id, true);
                auto effect = pair.first;
                auto parameter = pair.second;

                if (effect != nullptr && parameter != nullptr && parameter->lfo != nullptr && parameter->lfoRate != nullptr) {
                    if (effect->enabled != nullptr && effect->getId() == parameter->paramID) {
                        effect->enabled->setBoolValueNotifyingHost(selected);
                    }
                    parameter->lfo->setUnnormalisedValueNotifyingHost((int) lfoType);
                    parameter->lfoRate->setUnnormalisedValueNotifyingHost(lfoRate);
                }
            }
        }

        updateEffectPrecedence();

        auto perspectiveFunction = xml->getChildByName("depthFunction");
        if (perspectiveFunction != nullptr) {
            auto stream = juce::MemoryOutputStream();
            juce::Base64::convertFromBase64(stream, perspectiveFunction->getAllSubText());
            perspectiveEffect->updateCode(stream.toString());
        }

        auto fontFamilyXml = xml->getChildByName("fontFamily");
        if (fontFamilyXml != nullptr) {
            font.setTypefaceName(fontFamilyXml->getAllSubText());
        }
        auto fontStyleXml = xml->getChildByName("fontStyle");
        if (fontStyleXml != nullptr) {
            int style = fontStyleXml->getAllSubText().getIntValue();
            font.setBold(style == 1);
            font.setItalic(style == 2);
        }

        // close all files
        auto numFiles = fileBlocks.size();
        for (int i = 0; i < numFiles; i++) {
            removeFile(0);
        }

        auto filesXml = xml->getChildByName("files");
        if (filesXml != nullptr) {
            for (auto fileXml : filesXml->getChildIterator()) {
                auto nameXml = fileXml->getChildByName("name");
                auto dataXml = fileXml->getChildByName("data");
                auto fileName = nameXml != nullptr ? nameXml->getAllSubText() : "";
                auto data = dataXml != nullptr ? dataXml->getAllSubText() : "";

                auto stream = juce::MemoryOutputStream();
                juce::Base64::convertFromBase64(stream, data);
                auto fileBlock = std::make_shared<juce::MemoryBlock>(stream.getData(), stream.getDataSize());
                addFile(fileName, fileBlock);
            }
        }

        auto frameSourceXml = xml->getChildByName("frameSource");
        if (frameSourceXml != nullptr) {
            changeCurrentFile(frameSourceXml->getAllSubText().getIntValue());
        }
        broadcaster.sendChangeMessage();
    }
}

// gets the effect from the legacy id and optionally updates the precedence
std::pair<std::shared_ptr<Effect>, EffectParameter*> OscirenderAudioProcessor::effectFromLegacyId(const juce::String& id, bool updatePrecedence) {
    auto effectId = id;
    juce::String paramId = "";
    int precedence = -1;

    if (id == "vectorCancelling") {
        precedence = 0;
    } else if (id == "bitCrush") {
        precedence = 1;
    } else if (id == "verticalDistort") {
        precedence = 2;
        effectId = "distortY";
    } else if (id == "horizontalDistort") {
        precedence = 3;
        effectId = "distortX";
    } else if (id == "wobble") {
        precedence = 4;
    } else if (id == "smoothing") {
        precedence = 100;
    } else if (id == "rotateSpeed3d") {
        precedence = 52;
        effectId = "perspectiveStrength";
        paramId = "perspectiveRotateSpeed";
    } else if (id == "zPos") {
        precedence = 52;
        effectId = "perspectiveStrength";
        paramId = "perspectiveZPos";
    } else if (id == "imageRotateX") {
        precedence = 52;
        effectId = "perspectiveStrength";
        paramId = "perspectiveRotateX";
    } else if (id == "imageRotateY") {
        precedence = 52;
        effectId = "perspectiveStrength";
        paramId = "perspectiveRotateY";
    } else if (id == "imageRotateZ") {
        precedence = 52;
        effectId = "perspectiveStrength";
        paramId = "perspectiveRotateZ";
    } else if (id == "depthScale") {
        precedence = 52;
        effectId = "perspectiveStrength";
    } else if (id == "translationScale") {
        // this doesn't exist in the new version
    } else if (id == "translationSpeed") {
        // this doesn't exist in the new version
    } else if (id == "rotateSpeed") {
        precedence = 50;
        effectId = "2DRotateSpeed";
    } else if (id == "visibility") {
        // this doesn't exist in the new version
    } else if (id == "delayDecay") {
        precedence = 101;
    } else if (id == "delayEchoLength") {
        precedence = 101;
        effectId = "delayDecay";
        paramId = "delayLength";
    } else if (id == "bulge") {
        precedence = 53;
    } else if (id == "focalLength") {
        effectId = "objFocalLength";
    } else if (id == "objectXRotate") {
        effectId = "objRotateX";
    } else if (id == "objectYRotate") {
        effectId = "objRotateY";
    } else if (id == "objectZRotate") {
        effectId = "objRotateZ";
    } else if (id == "objectRotateSpeed") {
        effectId = "objRotateSpeed";
    } else if (id == "octave") {
        // this doesn't exist in the new version
    } else if (id == "micVolume") {
        // this doesn't exist in the new version
    } else if (id == "brightness") {
        // this doesn't exist in the new version
    }

    paramId = paramId == "" ? effectId : paramId;
    auto effect = getEffect(effectId);

    if (effect == nullptr) {
        return std::make_pair(nullptr, nullptr);
    } else {
        if (updatePrecedence) {
            effect->setPrecedence(precedence);
        }
        auto parameter = effect->getParameter(paramId);
        return std::make_pair(effect, parameter);
    }
}

LfoType OscirenderAudioProcessor::lfoTypeFromLegacyAnimationType(const juce::String& type) {
    if (type == "Static") {
        return LfoType::Static;
    } else if (type == "Sine") {
        return LfoType::Sine;
    } else if (type == "Square") {
        return LfoType::Square;
    } else if (type == "Seesaw") {
        return LfoType::Seesaw;
    } else if (type == "Linear") {
        return LfoType::Triangle;
    } else if (type == "Forward") {
        return LfoType::Sawtooth;
    } else if (type == "Reverse") {
        return LfoType::ReverseSawtooth;
    } else {
        return LfoType::Static;
    }
}

double OscirenderAudioProcessor::valueFromLegacy(double value, const juce::String& id) {
    if (id == "volume") {
        return value / 3.0;
    } else if (id == "frequency") {
        return std::pow(12000.0, value);
    }
    return value;
}