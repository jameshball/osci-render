#pragma once

#include "Document.h"
#include "ProjectSummary.h"
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
    // What this version couldn't use of it, left out (see LoadNotes).
    LoadNotes notes;
    // Why it couldn't be read at all, when it couldn't.
    enum class Problem { none, unreadable, notProject, newer, damaged };
    Problem problem = Problem::none;

    static constexpr juce::uint32 magic = 0x21324356;

    // The project in `data`, its sources prepared; when it isn't one, why,
    // and what can be done about it. `folder`, when known, is where the
    // project is: a linked file no longer where it was is looked for there,
    // as a project and its media moved together keep each other.
    static juce::Result read(const void* data, std::size_t size, ProjectFile& file, const std::atomic<bool>* cancel = nullptr, const juce::File& folder = {}) {
        // The header every saved project starts with (and every saved state).
        const auto headed = size >= 8 && juce::ByteOrder::littleEndianInt(data) == magic;
        if (!headed || size > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            file.problem = Problem::notProject;
            return juce::Result::fail("It isn't an osci-motion project. To draw a model, image, text or sound, add it to a project with Add source, or drop it on the window.");
        }
        file.xml = juce::AudioProcessor::getXmlFromBinary(data, static_cast<int>(size));
        if (file.xml != nullptr && file.xml->hasTagName("motion-project") && file.xml->getIntAttribute("schema") > 1) {
            file.problem = Problem::newer;
            return juce::Result::fail("It was saved by a newer osci-motion. Update osci-motion to open it.");
        }
        const auto* composition = file.xml != nullptr && file.xml->hasTagName("motion-project") && file.xml->getIntAttribute("schema") == 1 ? file.xml->getChildByName("composition") : nullptr;
        if (composition == nullptr) {
            file.problem = Problem::damaged;
            return damaged({});
        }
        if (folder.isDirectory()) { findMovedLinks(*file.xml, folder); }
        const auto prepared = Document::prepareLoad(*composition, file.project, cancel, &file.notes);
        if (prepared.failed() && !(cancel != nullptr && cancel->load())) {
            file.problem = Problem::damaged;
            return damaged(prepared.getErrorMessage());
        }
        return prepared;
    }
    static juce::Result read(const juce::File& source, ProjectFile& file, const std::atomic<bool>* cancel = nullptr) {
        juce::MemoryBlock bytes;
        if (!source.loadFileAsData(bytes)) {
            file.problem = Problem::unreadable;
            return juce::Result::fail("osci-motion couldn't read the file. Check that it is still there and that you can open it: a file kept in the cloud may need downloading first.");
        }
        return read(bytes.getData(), bytes.getSize(), file, cancel, source.getParentDirectory());
    }
    // Each linked file not where the project says, but beside the project
    // under its own name: there instead.
    static void findMovedLinks(juce::XmlElement& xml, const juce::File& folder) {
        for (auto* child : xml.getChildIterator()) {
            const auto path = child->hasTagName("asset") ? child->getStringAttribute("file") : juce::String();
            if (path.isNotEmpty() && juce::File::isAbsolutePath(path) && !juce::File(path).existsAsFile()) {
                const auto beside = folder.getChildFile(juce::File(path).getFileName());
                if (beside.existsAsFile()) { child->setAttribute("file", beside.getFullPathName()); }
            }
            findMovedLinks(*child, folder);
        }
    }
    // Damaged past what loading leaves out (see LoadNotes): `reason` says how.
    static juce::Result damaged(const juce::String& reason) {
        const auto how = reason.trim().trimCharactersAtEnd(".");
        return juce::Result::fail("The file is damaged" + (how.isEmpty() ? juce::String() : " (" + how + ")") + ", so it can't be opened. An earlier copy of it, from a backup, may open.");
    }
    // A project on its own (no editor or Scope settings with it), as the app
    // writes one, to open in the app or read back; with its picture (a JPEG,
    // see ProjectSummary) when given one.
    static juce::Result write(const Project& project, const juce::File& destination, const juce::MemoryBlock& picture = {}) {
        juce::XmlElement xml("motion-project");
        xml.setAttribute("schema", 1);
        xml.addChildElement(new juce::XmlElement(Document::save(project)));
        ProjectSummary::write(xml, picture, project.description);
        juce::MemoryBlock bytes;
        juce::AudioProcessor::copyXmlToBinary(xml, bytes);
        if (!destination.replaceWithData(bytes.getData(), bytes.getSize())) { return juce::Result::fail("Couldn't write " + destination.getFullPathName() + "."); }
        return juce::Result::ok();
    }
};
}
