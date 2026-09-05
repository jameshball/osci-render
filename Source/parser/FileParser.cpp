#include "FileParser.h"
#include "FileFormatRegistry.h"
#include <numbers>
#include "../CommonPluginEditor.h"
#include "../PluginProcessor.h"
#include "../components/OverlayDialogHelpers.h"

#if OSCI_PREMIUM
#include "lottie/DotLottieArchive.h"
#endif

#if OSCI_PREMIUM
namespace {
bool looksLikeLottieJson(const juce::String& jsonContent) {
	auto parsed = juce::JSON::parse(jsonContent);
	auto* object = parsed.getDynamicObject();
	if (object == nullptr) {
		return false;
	}

	return object->hasProperty(juce::Identifier("v"))
		&& object->hasProperty(juce::Identifier("fr"))
		&& object->hasProperty(juce::Identifier("op"))
		&& object->getProperty(juce::Identifier("layers")).isArray();
}

void showLottieLoadError(OscirenderAudioProcessor& processor, juce::String title, juce::String message) {
	juce::Component::SafePointer<CommonPluginEditor> editor(dynamic_cast<CommonPluginEditor*>(processor.getActiveEditor()));
	juce::MessageManager::callAsync([editor, title = std::move(title), message = std::move(message)] {
		osci::showOverlayMessageOrAlert(editor.getComponent(),
			title,
			message,
			osci::ErrorOverlay::Icon::Warning,
			juce::MessageBoxIconType::WarningIcon,
			{ 500, 260 });
	});
}

}
#endif

FileParser::FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback)
    : audioProcessor(p), errorCallback(errorCallback) {}

void FileParser::clearLoadedSource() {
	++sourceGeneration;
	isAnimatable = false;
	sampleSource = false;
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
}

// Helper function to show file size warning
void FileParser::showFileSizeWarning(juce::String fileName, int64_t totalBytes, int64_t mbLimit,
	juce::String fileType, std::function<void()> callback) {

	if (totalBytes <= mbLimit * 1024 * 1024) {
		callback();
		return;
	}

	const double fileSizeMB = totalBytes / (1024.0 * 1024.0);
	juce::String message = "The " + fileType + " file '" + fileName + "' you're trying to open is " + juce::String(fileSizeMB, 2) + " MB in size, and may take a long time to open.\n\nWould you like to continue loading it?";
	
	auto weakThis = weak_from_this();
	const auto generation = sourceGeneration.load();
	juce::MessageManager::callAsync([weakThis, generation, message, callback] {
		auto parser = weakThis.lock();
		if (parser == nullptr || parser->sourceGeneration != generation) {
			return;
		}
		auto* editor = dynamic_cast<CommonPluginEditor*>(parser->audioProcessor.getActiveEditor());
		osci::showOverlayConfirmationOrAlert(
			editor,
			"Large File",
			message,
			"Continue",
			"Cancel",
			[weakThis, generation, callback] {
				auto parser = weakThis.lock();
				if (parser == nullptr) {
					return;
				}
				{
					juce::SpinLock::ScopedLockType fileLock(parser->audioProcessor.getFileController().lock);
					juce::SpinLock::ScopedLockType effectLock(parser->audioProcessor.effectsLock);
					juce::SpinLock::ScopedLockType scope(parser->lock);
					if (parser->sourceGeneration != generation) {
						return;
					}
					callback();
				}
				parser->audioProcessor.getFileController().sendChangeMessage();
			},
			[weakThis, generation] {
				auto parser = weakThis.lock();
				if (parser != nullptr && parser->sourceGeneration == generation) {
					parser->audioProcessor.getFileController().removeParser(parser.get());
				}
			},
			osci::ErrorOverlay::Icon::Warning,
			{ 520, 330 });
	});
}

void FileParser::parse(juce::String fileId, juce::String fileName, juce::String extension, std::unique_ptr<juce::InputStream> stream, juce::Font font) {
	juce::SpinLock::ScopedLockType scope(lock);

	if (extension == ".lua" && lua != nullptr && lua->isFunctionValid()) {
		fallbackLuaScript = lua->getScript();
	}

	clearLoadedSource();

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
		int bytesRead = stream->readIntoMemoryBlock(buffer);
		if (bytesRead < 8) return;
		char* gplaData = (char*)buffer.getData();
		const char tag[] = "GPLA    ";
		bool isBinary = true;
		for (int i = 0; i < 8; i++) {
			isBinary = isBinary && tag[i] == gplaData[i];
		}
		if (isBinary) {
			gpla = std::make_shared<LineArtParser>(gplaData, bytesRead);
		} else {
			stream->setPosition(0);
			gpla = std::make_shared<LineArtParser>(stream->readEntireStreamAsString());
		}
		frameRate.store(gpla->getFrameRate(), std::memory_order_relaxed);
	} else if (osci::files::isImage(extension)) {
		juce::MemoryBlock buffer{};
		int bytesRead = stream->readIntoMemoryBlock(buffer);

		auto loadImage = [this, buffer, extension] {
			img = std::make_shared<ImageParser>(audioProcessor, extension, buffer);
			frameRate.store(img->getFrameRate(), std::memory_order_relaxed);
			isAnimatable = osci::files::isAnimated(extension);
			sampleSource = true;
		};
		showFileSizeWarning(fileName, bytesRead, 20, osci::files::isVideo(extension) ? "video" : "image",
			[this, loadImage, extension] {
#if OSCI_PREMIUM
				if (osci::files::isVideo(extension) && !audioProcessor.getFFmpegFile().existsAsFile()) {
					auto weakThis = weak_from_this();
					const auto generation = sourceGeneration.load();
					juce::MessageManager::callAsync([weakThis, generation, loadImage] {
						auto parser = weakThis.lock();
						if (parser == nullptr || parser->sourceGeneration != generation) {
							return;
						}
						parser->audioProcessor.ensureFFmpegExists(nullptr, [weakThis, generation, loadImage] {
							auto parser = weakThis.lock();
							if (parser == nullptr) {
								return;
							}
							{
								juce::SpinLock::ScopedLockType fileLock(parser->audioProcessor.getFileController().lock);
								juce::SpinLock::ScopedLockType effectLock(parser->audioProcessor.effectsLock);
								juce::SpinLock::ScopedLockType scope(parser->lock);
								if (parser->sourceGeneration != generation) {
									return;
								}
								loadImage();
							}
							parser->audioProcessor.getFileController().sendChangeMessage();
						});
					});
					return;
				}
#endif
				loadImage();
			});
	} else if (extension == ".lsystem") {
#if OSCI_PREMIUM
		fractal = std::make_shared<FractalParser>(stream->readEntireStreamAsString());
#endif
#if OSCI_PREMIUM
	} else if (osci::files::isLottie(extension)) {
		auto buffer = std::make_shared<juce::MemoryBlock>();
		int bytesRead = stream->readIntoMemoryBlock(*buffer);
		showFileSizeWarning(fileName, bytesRead, 10, "Lottie", [this, buffer, extension] {
			juce::String jsonContent;
			if (extension == ".lottie") {
				jsonContent = osci::lottie::extractAnimationJsonFromDotLottie(*buffer);
				if (jsonContent.isEmpty()) {
					showLottieLoadError(audioProcessor,
						"Error Loading Lottie",
						"The .lottie archive did not contain a Lottie animation JSON.");
					return;
				}
			} else {
				jsonContent = juce::String::fromUTF8(static_cast<const char*>(buffer->getData()),
					(int)buffer->getSize());
			}

			if (!looksLikeLottieJson(jsonContent)) {
				showLottieLoadError(audioProcessor,
					"Unsupported JSON",
					"The selected JSON file does not look like a Lottie animation.");
				return;
			}

			lottie = std::make_shared<OsciLottieParser>(jsonContent, [this](juce::String message) {
				showLottieLoadError(audioProcessor, "Error Loading Lottie", std::move(message));
			});
			frameRate.store(lottie->getFrameRate(), std::memory_order_relaxed);
			isAnimatable = true;
			sampleSource = false;
		});
#endif
	} else if (osci::files::isAudio(extension)) {
		wav = std::make_shared<WavParser>([this] { return audioProcessor.currentSampleRate.load(); });
		if (!wav->parse(std::move(stream))) {
			juce::Component::SafePointer<CommonPluginEditor> editor(dynamic_cast<CommonPluginEditor*>(audioProcessor.getActiveEditor()));
			juce::MessageManager::callAsync([editor, fileName] {
				osci::showOverlayMessageOrAlert(editor.getComponent(),
					"Error Loading " + fileName,
					"The audio file '" + fileName + "' could not be loaded.",
					osci::ErrorOverlay::Icon::Warning,
					juce::MessageBoxIconType::WarningIcon,
					{ 500, 260 });
			});
		}
	}

	isAnimatable = gpla != nullptr || (img != nullptr && osci::files::isAnimated(extension));
#if OSCI_PREMIUM
	isAnimatable = isAnimatable || lottie != nullptr;
#endif
	sampleSource = lua != nullptr || img != nullptr || wav != nullptr;
}

void FileParser::prepareLiveImageInput(int width, int height) {
	auto imageParser = std::make_shared<ImageParser>(audioProcessor, width, height);

	juce::SpinLock::ScopedLockType scope(lock);

	clearLoadedSource();
	img = std::move(imageParser);
	isAnimatable = false;
	sampleSource = true;
	active = true;
}

void FileParser::updateLiveImageFrame(const std::vector<std::uint8_t>& rgba, int width, int height, bool verticallyFlipped) {
	std::shared_ptr<ImageParser> imageParser;
	{
		juce::SpinLock::ScopedLockType scope(lock);
		imageParser = img;
	}

	if (imageParser != nullptr) {
		imageParser->setSingleFrameFromRgba(rgba, width, height, verticallyFlipped);
	}
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

osci::Point FileParser::nextSample(LuaState& L, LuaVariables& vars) {
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
	if (img != nullptr) {
		return img->getFrameRate();
	}
	return frameRate.load(std::memory_order_relaxed);
}
