#pragma once

#include "Document.h"
#include <atomic>
#include <limits>
#include <memory>

namespace motion {
// A saved project as osci-motion writes it: XML behind a binary header, with
// the composition and its sources in it. Read the same way wherever one is
// opened: the editor's Open, a host restoring its state, the command line;
// and written by the command line as the app writes it.
struct ProjectFile {
    std::unique_ptr<juce::XmlElement> xml;
    Project project;

    // The project in `data`, its sources prepared; a reason when it isn't one.
    static juce::Result read(const void* data, std::size_t size, ProjectFile& file, const std::atomic<bool>* cancel = nullptr) {
        if (size > static_cast<std::size_t>(std::numeric_limits<int>::max())) { return juce::Result::fail("It is too big to be an osci-motion project."); }
        file.xml = juce::AudioProcessor::getXmlFromBinary(data, static_cast<int>(size));
        if (file.xml == nullptr || !file.xml->hasTagName("motion-project") || file.xml->getIntAttribute("schema") != 1) { return juce::Result::fail("It isn't an osci-motion project this version can read."); }
        const auto* composition = file.xml->getChildByName("composition");
        if (composition == nullptr) { return juce::Result::fail("It has no composition."); }
        return Document::prepareLoad(*composition, file.project, cancel);
    }
    static juce::Result read(const juce::File& source, ProjectFile& file, const std::atomic<bool>* cancel = nullptr) {
        juce::MemoryBlock bytes;
        if (!source.loadFileAsData(bytes)) { return juce::Result::fail("Couldn't read the project file."); }
        return read(bytes.getData(), bytes.getSize(), file, cancel);
    }
    // A project on its own (no editor or Scope settings with it), as the app
    // writes one, to open in the app or read back.
    static juce::Result write(const Project& project, const juce::File& destination) {
        juce::XmlElement xml("motion-project");
        xml.setAttribute("schema", 1);
        xml.addChildElement(new juce::XmlElement(Document::save(project)));
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary(xml, bytes);
        if (!destination.replaceWithData(bytes.getData(), bytes.getSize())) { return juce::Result::fail("Couldn't write " + destination.getFullPathName() + "."); }
        return juce::Result::ok();
    }
};
}
