#include "FeedbackReportBuilder.h"

#include "../CommonPluginProcessor.h"
#include "../JucewrightAutomation.h"

namespace {
juce::var makeClientContext(CommonAudioProcessor& processor) {
    auto* object = new juce::DynamicObject();
    object->setProperty("juce_version", juce::SystemStats::getJUCEVersion());
    object->setProperty("renderer", "opengl");
    object->setProperty("sample_rate", processor.getSampleRate());
    object->setProperty("block_size", processor.getBlockSize());
    object->setProperty("input_channels", processor.getTotalNumInputChannels());
    object->setProperty("output_channels", processor.getTotalNumOutputChannels());
    return juce::var(object);
}

juce::String readSanitizedLogSnapshot(const CommonAudioProcessor& processor, bool& truncated) {
    constexpr size_t maxBytes = 256 * 1024;
    constexpr juce::int64 maxSourceBytes = 1024 * 1024;
    truncated = false;

    const auto logFile = processor.applicationFolder.getChildFile(juce::String(JucePlugin_Name) + ".log");
    juce::FileInputStream stream(logFile);
    if (!stream.openedOk()) {
        return {};
    }

    const auto sourceStart = juce::jmax<juce::int64>(0, stream.getTotalLength() - maxSourceBytes);
    stream.setPosition(sourceStart);
    auto log = stream.readEntireStreamAsString();
    if (sourceStart > 0) {
        truncated = true;
        log = log.fromFirstOccurrenceOf("\n", false, false);
    }
    if (log.isEmpty()) {
        return {};
    }

    const juce::StringArray safePrefixes {
        "==== ",
        "Version: ",
        "Wrapper: ",
        "JUCE: ",
        "OS: ",
        "CPU: ",
        "RAM: ",
        "prepareToPlay: ",
        "getStateInformation: ",
        "MidiCCManager::save: "
    };
    juce::StringArray lines;
    lines.addLines(log);
    juce::String sanitized;
    for (const auto& line : lines) {
        const auto trimmed = line.trimStart();
        for (const auto& prefix : safePrefixes) {
            if (trimmed.startsWith(prefix)) {
                sanitized << trimmed << "\n";
                break;
            }
        }
    }

    while (static_cast<size_t>(sanitized.getNumBytesAsUTF8()) > maxBytes && sanitized.isNotEmpty()) {
        truncated = true;
        sanitized = sanitized.substring(juce::jmin(4096, sanitized.length()));
    }
    return sanitized;
}

juce::String binarySvg(const char* data, int size) {
    return juce::String::createStringFromData(data, size);
}
} // namespace

osci::FeedbackOverlayConfig FeedbackReportBuilder::create(CommonAudioProcessor& processor,
                                                           juce::Component& screenshotSource,
                                                           juce::String projectFileType) {
    osci::FeedbackOverlayConfig feedback;
    feedback.closeButtonSvg = binarySvg(BinaryData::close_svg, BinaryData::close_svgSize);
    feedback.settingsButtonSvg = binarySvg(BinaryData::cog_svg, BinaryData::cog_svgSize);
    feedback.magnifierSvg = binarySvg(BinaryData::magnify_svg, BinaryData::magnify_svgSize);

#if OSCI_PREMIUM
    const juce::String productVariant = "premium";
#else
    const juce::String productVariant = "free";
#endif

    osci::FeedbackContextBuilder(feedback.context)
        .withProduct(processor.getProductSlug(), ProjectInfo::versionString, productVariant)
        .withContactEmailFrom(processor.licenseManager);

    const auto wrapperType = processor.wrapperType;
    const auto displayInfo = osci::FeedbackContextBuilder::displayInfoFor(screenshotSource);
    feedback.submissionProvider = [processor = &processor, wrapperType, displayInfo](
                                      osci::FeedbackRequest& request,
                                      osci::FeedbackAttachmentData& projectSnapshot,
                                      osci::FeedbackOverlayConfig::SubmissionOptions options) {
        osci::FeedbackContextBuilder(request)
            .withSystemInfo()
            .withPluginHost(wrapperType)
            .withDisplay(displayInfo)
            .withReleaseTrack()
            .withValidLicenseTokenFrom(processor->licenseManager);
        request.clientContextSchemaVersion = 1;
        request.clientContext = makeClientContext(*processor);
        if (options.includeDiagnosticLog) {
            request.log = readSanitizedLogSnapshot(*processor, request.logTruncated);
        }
        if (options.includeProjectSnapshot && projectSnapshot.data.isEmpty()) {
            processor->getPortableProjectSnapshot(projectSnapshot.data);
        }
    };

    auto screenshot = screenshotSource.createComponentSnapshot(screenshotSource.getLocalBounds(), true, 1.0f);
    feedback.automaticScreenshotPreview = screenshot;
    feedback.automaticScreenshot.kind = osci::FeedbackAttachmentKind::screenshot;
    feedback.automaticScreenshot.filename = feedback.context.productSlug + "-ui.png";
    feedback.automaticScreenshot.contentType = "image/png";

    feedback.projectSnapshot.kind = osci::FeedbackAttachmentKind::project;
    feedback.projectSnapshot.filename = feedback.context.productSlug + "-feedback." + std::move(projectFileType);
    feedback.projectSnapshot.contentType = "application/octet-stream";

#if DEBUG
    const auto automationBaseUrl = juce::SystemStats::getEnvironmentVariable("OSCI_FEEDBACK_API_BASE_URL", {});
    if (osci::isJucewrightAutomationLaunch() && automationBaseUrl.isNotEmpty()) {
        feedback.backend.apiBaseUrl = automationBaseUrl;
    }
#endif
    return feedback;
}
