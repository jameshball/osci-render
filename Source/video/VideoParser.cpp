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

	videoEngine.getFormatManager().registerFormat(std::make_unique<foleys::FFmpegFormat>());
	std::shared_ptr<foleys::AVClip> clip = videoEngine.createClipFromFile(juce::URL(file));
}

VideoParser::~VideoParser() {
}
