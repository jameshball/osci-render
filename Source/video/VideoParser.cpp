#include "VideoParser.h"
#include "../svg/SvgParser.h"


VideoParser::VideoParser(juce::String fileName, juce::MemoryBlock data) {
	juce::TemporaryFile tempFile{ fileName };
	juce::File file{ tempFile.getFile() };
	juce::FileOutputStream output{ file };
	
    if (output.openedOk()) {
        output.setPosition(0);
        output.truncate();
        output.write(data.getData(), data.getSize());
    }

	std::shared_ptr<foleys::AVClip> clip = videoEngine.createClipFromFile(file.getFullPathName());
}

VideoParser::~VideoParser() {
}
