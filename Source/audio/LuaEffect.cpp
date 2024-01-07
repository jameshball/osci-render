#include "LuaEffect.h"
#include "../lua/LuaParser.h"

Point LuaEffect::apply(int index, Point input, const std::vector<double>& values, double sampleRate) {
	int fileIndex = audioProcessor.getCurrentFileIndex();
	if (fileIndex == -1) {
		return input;
	}
	std::shared_ptr<LuaParser> parser = audioProcessor.getCurrentFileParser()->getLua();
	if (parser != nullptr) {
		parser->setVariable("slider_" + name.toLowerCase(), values[0]);
	}
	
	audioProcessor.perspectiveEffect->setVariable("slider_" + name.toLowerCase(), values[0]);

	return input;
}
