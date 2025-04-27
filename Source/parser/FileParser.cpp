#include "FileParser.h"
#include <numbers>
#include "../PluginProcessor.h"

FileParser::FileParser(OscirenderAudioProcessor &p, std::function<void(int, juce::String, juce::String)> errorCallback) 
    : errorCallback(errorCallback), audioProcessor(p) {}

// Helper function to show file size warning
void FileParser::showFileSizeWarning(juce::String fileName, int64_t totalBytes, int64_t mbLimit,
	juce::String fileType, std::function<void()> callback) {

	if (totalBytes <= mbLimit * 1024 * 1024) {
		callback();
		return;
	}

	const double fileSizeMB = totalBytes / (1024.0 * 1024.0);
	juce::String message = juce::String::formatted(
		"The %s file '%s' you're trying to open is %.2f MB in size, and may time a long time to open. "
		"Would you like to continue loading it?", fileType.toRawUTF8(), fileName.toRawUTF8(), fileSizeMB);
	
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
		text = std::make_shared<TextParser>(audioProcessor, stream->readEntireStreamAsString(), font);
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
	} else if (extension == ".gif" || extension == ".png" || extension == ".jpg" || extension == ".jpeg" || extension == ".mp4" || extension == ".mov") {
		juce::MemoryBlock buffer{};
		int bytesRead = stream->readIntoMemoryBlock(buffer);

		showFileSizeWarning(fileName, bytesRead, 20, (extension == ".mp4" || extension == ".mov") ? "video" : "image",
			[this, buffer, extension]() {
				img = std::make_shared<ImageParser>(audioProcessor, extension, buffer);
                isAnimatable = extension == ".gif" || extension == ".mp4" || extension == ".mov";
                sampleSource = true;
			}
		);
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
        auto values = lua->run(L, vars);
        if (values.size() == 2) {
            return osci::Point(values[0], values[1], 0);
        } else if (values.size() > 2) {
            return osci::Point(values[0], values[1], values[2]);
        }
    } else if (img != nullptr) {
        return img->getSample();
    } else if (wav != nullptr) {
        return wav->getSample();
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

int FileParser::getNumFrames() {
    if (gpla != nullptr) {
        return gpla->numFrames;
    } else if (img != nullptr) {
        return img->getNumFrames();
    }
    return 1; // Default to 1 frame for non-animatable content
}

int FileParser::getCurrentFrame() {
    if (gpla != nullptr) {
        return gpla->frameNumber;
    } else if (img != nullptr) {
        return img->getCurrentFrame();
    }
    return 0; // Default to frame 0 for non-animatable content
}

void FileParser::setFrame(int frame) {
    if (gpla != nullptr) {
        gpla->setFrame(frame);
    } else if (img != nullptr) {
        img->setFrame(frame);
    }
}
