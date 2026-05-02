#include "FileParser.h"
#include <numbers>
#include "../PluginProcessor.h"
#if OSCI_PREMIUM
#include "lottie/DotLottieArchive.h"
#endif

#if OSCI_PREMIUM
namespace {
bool looksLikeLottieJson(const juce::String& jsonContent) {
	auto parsed = juce::JSON::parse(jsonContent);
	auto* object = parsed.getDynamicObject();
	if (object == nullptr) return false;

	return object->hasProperty(juce::Identifier("v"))
		&& object->hasProperty(juce::Identifier("fr"))
		&& object->hasProperty(juce::Identifier("op"))
		&& object->getProperty(juce::Identifier("layers")).isArray();
}

void showInvalidLottieJsonWarning() {
	juce::MessageManager::callAsync([] {
		juce::AlertWindow::showMessageBoxAsync(
			juce::AlertWindow::AlertIconType::WarningIcon,
			"Unsupported JSON",
			"The selected JSON file does not look like a Lottie animation.");
	});
}
}
#endif

FileParser::FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback) 
    : audioProcessor(p), errorCallback(errorCallback) {}

// Helper function to show file size warning
void FileParser::showFileSizeWarning(juce::String fileName, int64_t totalBytes, int64_t mbLimit,
	juce::String fileType, std::function<void()> callback) {

	if (totalBytes <= mbLimit * 1024 * 1024) {
		callback();
		return;
	}

	const double fileSizeMB = totalBytes / (1024.0 * 1024.0);
	juce::String message = "The " + fileType + " file '" + fileName + "' you're trying to open is " + juce::String(fileSizeMB, 2) + " MB in size, and may take a long time to open.\n\nWould you like to continue loading it?";
	
	juce::MessageManager::callAsync([this, message, callback]() {
		juce::AlertWindow::showOkCancelBox(
			juce::AlertWindow::WarningIcon,
			"Large File",
			message,
			"Continue",
			"Cancel",
			nullptr,
			juce::ModalCallbackFunction::create([this, callback](int result) {
				juce::SpinLock::ScopedLockType scope(lock);
				if (result == 1) { // 1 = OK button pressed
					callback();
				} else {
					disable(); // Mark this parser as inactive
					
					// Notify the processor to remove this parser
					juce::MessageManager::callAsync([this] {
						juce::SpinLock::ScopedLockType lock1(audioProcessor.parsersLock);
						juce::SpinLock::ScopedLockType lock2(audioProcessor.effectsLock);
						audioProcessor.removeParser(this);
					});
				}
			})
		);
	});
}

void FileParser::parse(juce::String fileId, juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (extension == ".lua" && lua != nullptr && lua->isFunctionValid()) {
		fallbackLuaScript = lua->getScript();
	}

	object = nullptr;
	svg = nullptr;
	text = nullptr;
	gpla = nullptr;
	lua = nullptr;
	img = nullptr;
	wav = nullptr;
	frameRate.store(30.0, std::memory_order_relaxed);
#if OSCI_PREMIUM
	fractal = nullptr;
	lottie = nullptr;
#endif
	
	if (extension == ".obj") {
		const int64_t fileSize = stream->getTotalLength();
		juce::String objContent = stream->readEntireStreamAsString();
		showFileSizeWarning(fileName, fileSize, 1, "OBJ", [this, objContent]() {
			object = std::make_shared<WorldObject>(objContent.toStdString());
            isAnimatable = false;
            sampleSource = false;
		});
	} else if (extension == ".svg") {
		svg = std::make_shared<SvgParser>(stream->readEntireStreamAsString());
	} else if (extension == ".txt") {
        text = std::make_shared<TextParser>(stream->readEntireStreamAsString(), audioProcessor.font);
	} else if (extension == ".lua") {
		lua = std::make_shared<LuaParser>(fileId, stream->readEntireStreamAsString(), errorCallback, fallbackLuaScript);
	} else if (extension == ".gpla") {
		juce::MemoryBlock buffer{};
		auto bytesRead = stream->readIntoMemoryBlock(buffer);
		if (bytesRead < 8) return;
		const char* gplaData = static_cast<const char*>(buffer.getData());
		const char tag[] = "GPLA    ";
		bool isBinary = true;
		for (int i = 0; i < 8; i++) {
			isBinary = isBinary && tag[i] == gplaData[i];
		}
		if (isBinary) {
			gpla = std::make_shared<LineArtParser>(gplaData, static_cast<int>(bytesRead));
		} else {
			stream->setPosition(0);
			gpla = std::make_shared<LineArtParser>(stream->readEntireStreamAsString());
		}
		frameRate.store(gpla->getFrameRate(), std::memory_order_relaxed);
	} else if (extension == ".gif" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".mp4" || extension == ".mov") {
		juce::MemoryBlock buffer{};
		auto bytesRead = stream->readIntoMemoryBlock(buffer);

		showFileSizeWarning(fileName, bytesRead, 20, (extension == ".mp4" || extension == ".mov") ? "video" : "image",
			[this, buffer, extension]() {
				img = std::make_shared<ImageParser>(audioProcessor, extension, buffer);
                frameRate.store(img->getFrameRate(), std::memory_order_relaxed);
                isAnimatable = extension == ".gif" || extension == ".mp4" || extension == ".mov";
                sampleSource = true;
			}
		);
	} else if (extension == ".lsystem") {
#if OSCI_PREMIUM
		fractal = std::make_shared<FractalParser>(stream->readEntireStreamAsString());
#endif
       } else if (isLottieExtension(extension)) {
#if OSCI_PREMIUM
		auto buffer = std::make_shared<juce::MemoryBlock>();
		int bytesRead = stream->readIntoMemoryBlock(*buffer);
		showFileSizeWarning(fileName, bytesRead, 10, "Lottie", [this, buffer, extension]() {
			juce::String jsonContent;
			if (extension == ".lottie") {
				jsonContent = osci::lottie::extractAnimationJsonFromDotLottie(*buffer);
				if (jsonContent.isEmpty()) {
					juce::MessageManager::callAsync([] {
						juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::AlertIconType::WarningIcon,
							"Error", "The .lottie archive did not contain a Lottie animation JSON.");
					});
					return;
				}
			} else {
				jsonContent = juce::String::fromUTF8(static_cast<const char*>(buffer->getData()),
					(int) buffer->getSize());
			}
			if (!looksLikeLottieJson(jsonContent)) {
				showInvalidLottieJsonWarning();
				return;
			}
			lottie = std::make_shared<OsciLottieParser>(jsonContent);
			frameRate.store(lottie->getFrameRate(), std::memory_order_relaxed);
			isAnimatable = true;
			sampleSource = false;
		});
#endif
	} else if (extension == ".wav" || extension == ".aiff" || extension == ".flac" || extension == ".ogg" || extension == ".mp3") {
		wav = std::make_shared<WavParser>(audioProcessor);
		if (!wav->parse(std::move(stream))) {
			juce::MessageManager::callAsync([this, fileName] {
				juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::AlertIconType::WarningIcon,
					"Error Loading " + fileName,
					"The audio file '" + fileName + "' could not be loaded.");
			});
		}
	}

	isAnimatable = gpla != nullptr || (img != nullptr && (extension == ".gif" || extension == ".mp4" || extension == ".mov"));
#if OSCI_PREMIUM
	isAnimatable = isAnimatable || lottie != nullptr;
#endif
	sampleSource = lua != nullptr || img != nullptr || wav != nullptr;
}

std::vector<std::unique_ptr<osci::Shape>> FileParser::nextFrame() {
    juce::SpinLock::ScopedLockType scope(lock);

    if (object != nullptr) {
        return object->draw();
    } else if (svg != nullptr) {
        return svg->draw();
    } else if (text != nullptr) {
        return text->draw();
    } else if (gpla != nullptr) {
        return gpla->draw();
    }
#if OSCI_PREMIUM
    else if (lottie != nullptr) {
        return lottie->draw();
    }
    else if (fractal != nullptr) {
        fractal->setIterations(juce::roundToInt(audioProcessor.fractalDepthEffect->getActualValue()));
        return fractal->draw();
    }
#endif
    auto tempShapes = std::vector<std::unique_ptr<osci::Shape>>();
    // return a square
    tempShapes.push_back(std::make_unique<osci::Line>(osci::Point(-0.5, -0.5, 0), osci::Point(0.5, -0.5, 0)));
    tempShapes.push_back(std::make_unique<osci::Line>(osci::Point(0.5, -0.5, 0), osci::Point(0.5, 0.5, 0)));
    tempShapes.push_back(std::make_unique<osci::Line>(osci::Point(0.5, 0.5, 0), osci::Point(-0.5, 0.5, 0)));
    tempShapes.push_back(std::make_unique<osci::Line>(osci::Point(-0.5, 0.5, 0), osci::Point(-0.5, -0.5, 0)));
    return tempShapes;
}

osci::Point FileParser::nextSample(lua_State*& L, LuaVariables& vars) {
    juce::SpinLock::ScopedLockType scope(lock);

    if (lua != nullptr) {
        auto result = lua->run(L, vars);
        if (result.count >= 6) {
            return osci::Point(result.values[0], result.values[1], result.values[2], result.values[3], result.values[4], result.values[5]);
        } else if (result.count >= 3) {
            return osci::Point(result.values[0], result.values[1], result.values[2]);
        } else if (result.count == 2) {
            return osci::Point(result.values[0], result.values[1]);
        }
    } else if (img != nullptr) {
        return img->getSample(vars.blockSampleIndex);
    } else if (wav != nullptr) {
        wavPointBuffer.clear();
        wav->processBlock(wavPointBuffer);
        auto* data = wavPointBuffer.getReadPointer(0);
		return osci::Point(data[0], data[1], data[2]);
    }

    return osci::Point();
}

bool FileParser::isSample() {
    return sampleSource;
}

bool FileParser::isActive() {
    return active;
}

void FileParser::disable() {
    active = false;
}

void FileParser::enable() {
    active = true;
}

bool FileParser::consumeDirty() {
    juce::SpinLock::ScopedLockType scope(lock);
#if OSCI_PREMIUM
    if (fractal != nullptr) return fractal->consumeDirty();
#endif
    return false;
}

std::shared_ptr<WorldObject> FileParser::getObject() {
    return object;
}

std::shared_ptr<SvgParser> FileParser::getSvg() {
    return svg;
}

std::shared_ptr<TextParser> FileParser::getText() {
    return text;
}

std::shared_ptr<LineArtParser> FileParser::getLineArt() {
    return gpla;
}

std::shared_ptr<LuaParser> FileParser::getLua() {
    return lua;
}

std::shared_ptr<ImageParser> FileParser::getImg() {
    return img;
}

std::shared_ptr<WavParser> FileParser::getWav() {
    return wav;
}

#if OSCI_PREMIUM
std::shared_ptr<FractalParser> FileParser::getFractal() {
    return fractal;
}

std::shared_ptr<OsciLottieParser> FileParser::getLottie() {
    return lottie;
}
#endif

int FileParser::getNumFrames() {
    if (gpla != nullptr) {
        return gpla->numFrames;
    } else if (img != nullptr) {
        return img->getNumFrames();
    }
#if OSCI_PREMIUM
    if (lottie != nullptr) {
        return lottie->getNumFrames();
    }
#endif
    return 1; // Default to 1 frame for non-animatable content
}

int FileParser::getCurrentFrame() {
    if (gpla != nullptr) {
        return gpla->frameNumber;
    } else if (img != nullptr) {
        return img->getCurrentFrame();
    }
#if OSCI_PREMIUM
    if (lottie != nullptr) {
        return lottie->getCurrentFrame();
    }
#endif
    return 0; // Default to frame 0 for non-animatable content
}

void FileParser::setFrame(int frame) {
    juce::SpinLock::ScopedLockType scope(lock);

    if (gpla != nullptr) {
        gpla->setFrame(frame);
    } else if (img != nullptr) {
        img->setFrame(frame);
    }
#if OSCI_PREMIUM
    else if (lottie != nullptr) {
        lottie->setFrame(frame);
    }
#endif
}

double FileParser::getFrameRate() const {
	return frameRate.load(std::memory_order_relaxed);
}
