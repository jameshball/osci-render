#pragma once

#include "../model/RasterSettings.h"
#include "../import/RasterSourcePreparer.h"
#include "../import/VideoSourcePreparer.h"
#include "../live/TextureSource.h"
#include "../import/BakedSourceArchive.h"
#include "Sheet.h"
#include "ScrubField.h"
#include "SourcePreview.h"
#include <functional>

// How an image or video is traced into beam strokes, the traced picture
// beside the settings, updated as they change: an image's first frame, or a
// frame FFmpeg picks from a video. For a picture another app shares, the
// settings apply as they change and the picture is the live one.
class MotionRasterSettingsPanel final : public motion::ui::Sheet {
public:
    struct Live {
        std::function<std::shared_ptr<const motion::PreparedSource>()> frame;
        std::function<juce::String()> status;
        std::function<void(motion::RasterSettings)> onChange;
        // Starts a capture, or ends one and keeps it.
        std::function<juce::Result()> onRecord;
        std::function<bool()> recording;
    };
    // `xyOnlyScope`: the project's scope has X and Y alone, so a Hilbert
    // curve shades by its density unless told otherwise; else by colour.
    // A video's frame comes from `videoFile` (or its bytes, in `image`) by
    // `ffmpeg`; without FFmpeg there is no picture.
    MotionRasterSettingsPanel(motion::RasterSettings initial, bool video, const juce::String& fileName, juce::MemoryBlock image = {}, bool xyOnlyScope = false,
            juce::File videoFile = {}, juce::File ffmpeg = {}, std::optional<Live> liveSource = std::nullopt)
        : Sheet(liveSource.has_value() ? motion::texture::technology() + " source" : video ? "Prepare video" : "Prepare image", fileName, liveSource.has_value() ? "Done" : "Prepare"),
          settings(initial), isVideo(video), data(std::move(image)), live(std::move(liveSource)) {
        if (isVideo && ffmpeg.existsAsFile() && (videoFile.existsAsFile() || data.getSize() > 0)) {
            still = std::make_shared<VideoStill>();
            still->video = videoFile;
            still->bytes = data;
            still->ffmpeg = ffmpeg;
        }
        setName(live.has_value() ? "Live picture settings" : "Image preparation settings");
        nameAction(live.has_value() ? "Done" : isVideo ? "Prepare video" : "Prepare image");
        detailEdited = initial.resolution != motion::RasterSettings().resolution || initial.mode == motion::RasterSettings::Mode::scanlines;
        mode.setSelected(initial.mode == motion::RasterSettings::Mode::scanlines ? 1 : initial.mode == motion::RasterSettings::Mode::hilbert ? 2 : 0);
        mode.setTooltip("Outlines follow image edges. Scanlines fill it with horizontal passes. Hilbert draws one curve, denser where brighter, which shows on any scope.");
        // Samples the other modes start with, back when leaving Hilbert.
        const auto otherSamples = initial.mode == motion::RasterSettings::Mode::hilbert ? motion::RasterSettings().pointsPerFrame : initial.pointsPerFrame;
        mode.onChange = [this, otherSamples](int index) {
            if (!detailEdited) { selectResolution(index == 1 ? 64 : motion::RasterSettings().resolution); }
            // A curve over the whole picture wants many samples.
            if (!samplesEdited) { selectSamples(index == 2 ? 16384 : otherSamples); }
            refresh();
        };
        if (initial.mode != motion::RasterSettings::Mode::hilbert) { settings.shading = xyOnlyScope ? motion::RasterSettings::Shading::density : motion::RasterSettings::Shading::colour; }
        shading.setSelected(settings.shading == motion::RasterSettings::Shading::colour ? 1 : 0);
        shading.setTooltip("Density shows brightness by how dense the curve is, on any scope. Brightness draws it in the picture's colours, for a scope with brightness (XYRGB).");
        shading.onChange = [this](int) { refresh(); };
        threshold.setName("Image threshold");
        threshold.setSpec(thresholdSpec);
        threshold.setValue(std::isfinite(initial.threshold) ? std::clamp(initial.threshold * 100, 0.0, 100.0) : 0);
        threshold.setTooltip("Pixels darker than this are blanked");
        threshold.onChange = [this](double) { thresholdEdited = true; refresh(); };
        threshold.onCommit = threshold.onChange;
        resolutions = {64, 128, 256, 512};
        strides = {512, 1024, 2048, 4096, 8192, 16384};
        if (std::find(resolutions.begin(), resolutions.end(), initial.resolution) == resolutions.end()) { resolutions.push_back(initial.resolution); }
        if (std::find(strides.begin(), strides.end(), initial.pointsPerFrame) == strides.end()) { strides.push_back(initial.pointsPerFrame); }
        detail.setName("Image detail");
        for (std::size_t i = 0; i < resolutions.size(); ++i) { detail.addItem(juce::String(resolutions[i]) + " px", static_cast<int>(i + 1)); }
        selectResolution(initial.resolution);
        detail.setTooltip("The size the image is traced at; lower simplifies outlines");
        detail.onChange = [this] { detailEdited = true; refresh(); };
        samples.setName("Image samples per frame");
        for (std::size_t i = 0; i < strides.size(); ++i) {
            samples.addItem(juce::String(static_cast<juce::int64>(strides[i])), static_cast<int>(i + 1));
            if (strides[i] == initial.pointsPerFrame) { samples.setSelectedId(static_cast<int>(i + 1), juce::dontSendNotification); }
        }
        samples.setTooltip("Increase this if the image is too complex");
        samples.onChange = [this] { samplesEdited = true; refresh(); };
        invert.setToggleState(initial.invert, juce::dontSendNotification);
        invert.setTooltip("For dark artwork on a light background");
        invert.onClick = [this] { refresh(); };
        frameRate.setName("Video bake frame rate");
        frameRate.setSpec(frameRateSpec);
        frameRate.setValue(initial.videoFrameRate);
        frameRate.onChange = [this](double value) { settings.videoFrameRate = value; refresh(); };
        frameRate.onCommit = frameRate.onChange;
        for (auto* field : {&threshold, &frameRate}) { field->setFill(fieldFill()); }
        mode.fill = fieldFill();
        shading.fill = fieldFill();
        for (auto* combo : {&detail, &samples}) {
            combo->setColour(juce::ComboBox::backgroundColourId, fieldFill());
            combo->setColour(juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        }
        for (auto* caption : {&modeLabel, &shadingLabel, &thresholdLabel, &detailLabel, &samplesLabel, &frameRateLabel, &invertLabel}) { styleCaption(*caption); }
        error.setFont(motion::style::body());
        error.setColour(juce::Label::textColourId, motion::style::error());
        error.setBorderSize({});
        error.setName("Image preparation validation error");
        primary.onClick = [this] {
            if (live.has_value()) {
                close();
                return;
            }
            if (submitted) { return; }
            refresh();
            if (valid && onPrepare) {
                submitted = true;
                primary.setEnabled(false);
                onPrepare(settings);
            }
        };
        for (auto* component : std::initializer_list<juce::Component*> {&modeLabel, &shadingLabel, &thresholdLabel, &detailLabel, &samplesLabel, &invertLabel,
                &mode, &shading, &threshold, &detail, &samples, &invert, &error}) {
            addAndMakeVisible(component);
        }
        if (isVideo) {
            addAndMakeVisible(frameRateLabel);
            addAndMakeVisible(frameRate);
        }
        if (hasPreview()) { addAndMakeVisible(preview); }
        if (live.has_value()) {
            // Changes apply as they're made, so there's nothing to cancel.
            cancel.setVisible(false);
            error.setVisible(false);
            record.setName("Record capture");
            record.setTooltip("Export needs a capture of a live picture");
            record.onClick = [this] {
                const auto result = live->onRecord ? live->onRecord() : juce::Result::fail("Recording isn't available.");
                if (result.failed()) { recordError = result.getErrorMessage(); }
                showLive();
            };
            addAndMakeVisible(record);
            liveFrames.startTimerHz(15);
        }
        const auto rows = isVideo ? 7 : 6;
        setSize(widthFor(hasPreview() ? bodyHeight(rows) : 0), heightFor(rows));
        liveApplied = settings;
        refresh();
    }

    std::function<void(motion::RasterSettings)> onPrepare;
    // A video already prepared here with these settings, as the cache it
    // is saved with, if one is.
    std::shared_ptr<const juce::MemoryBlock> preparedFor(const motion::RasterSettings& chosen) const {
        return bakedArchive != nullptr && same(bakedSettings, chosen) ? bakedArchive : nullptr;
    }
    ~MotionRasterSettingsPanel() override {
        if (bake != nullptr) { bake->cancel.store(true); }
        bakes.removeAllJobs(true, 10000);
    }

protected:
    void layoutBody(juce::Rectangle<int> area) override {
        if (hasPreview()) {
            preview.setBounds(area.removeFromLeft(area.getHeight()));
            area.removeFromLeft(24);
        }
        formRow(area, modeLabel, mode);
        formRow(area, shadingLabel, shading);
        formRow(area, thresholdLabel, threshold, number);
        formRow(area, detailLabel, detail);
        formRow(area, samplesLabel, samples);
        if (isVideo) { formRow(area, frameRateLabel, frameRate, number); }
        auto line = area.removeFromTop(row);
        invertLabel.setBounds(line.removeFromLeft(caption));
        invert.setBounds(line.removeFromLeft(motion::ui::Switch::width + 4));
        error.setBounds(footerLeft);
        if (live.has_value()) { record.setBounds(footerLeft.removeFromLeft(std::max(132, record.getBestWidthForHeight(footerLeft.getHeight()) + 20))); }
    }

private:
    static constexpr motion::PropertySpec thresholdSpec {"threshold", "Threshold", "", "", 0, 100, 2, .1, 1, "%"};
    static constexpr motion::PropertySpec frameRateSpec {"frameRate", "Frame rate", "", "", 1, 120, 30, .5, 3, " fps"};
    bool hasPreview() const { return live.has_value() || (isVideo ? still != nullptr : data.getSize() > 0); }
    std::optional<Live> live;
    // The settings last given to the live picture: its own at first (its
    // shading aside, which only a Hilbert curve uses).
    motion::RasterSettings liveApplied;
    juce::TextButton record;
    juce::String recordError;
    std::shared_ptr<const motion::PreparedSource> shownFrame;
    juce::String shownStatus;
    // The live picture as it is traced now, and how it's arriving.
    void showLive() {
        const auto recording = live->recording && live->recording();
        record.setButtonText(recording ? "Stop and keep" : "Record capture");
        // Red while it records, as a recorder's button is.
        if (recording) {
            record.setColour(juce::TextButton::buttonColourId, motion::style::error().withAlpha(.3f));
        } else {
            record.removeColour(juce::TextButton::buttonColourId);
        }
        const auto frame = live->frame ? live->frame() : nullptr;
        const auto status = recordError.isNotEmpty() ? recordError : live->status ? live->status() : juce::String();
        if (frame == shownFrame && status == shownStatus) { return; }
        shownFrame = frame;
        shownStatus = status;
        motion::ui::SourcePreview::Result result;
        if (frame != nullptr && frame->frameCount() > 0) { result.path = motion::ui::traceSource(*frame, 0, static_cast<int>(std::min<std::size_t>(settings.pointsPerFrame, 4096))); }
        result.caption = status;
        preview.show(std::move(result));
    }
    juce::TimedCallback liveFrames {[this] { showLive(); }};
    // A video's frame, fetched once for each detail on the preview's thread.
    struct VideoStill {
        juce::File video, ffmpeg;
        juce::MemoryBlock bytes;
        std::unique_ptr<juce::TemporaryFile> copy;
        int dimension = 0;
        std::vector<std::uint8_t> rgba;

        const std::vector<std::uint8_t>& at(int wanted, const std::atomic<bool>& cancel) {
            if (wanted == dimension) { return rgba; }
            if (!video.existsAsFile() && copy == nullptr) {
                copy = std::make_unique<juce::TemporaryFile>(".mov");
                copy->getFile().replaceWithData(bytes.getData(), bytes.getSize());
            }
            rgba = motion::VideoSourcePreparer::still(copy != nullptr ? copy->getFile() : video, ffmpeg, wanted, &cancel);
            dimension = rgba.empty() || cancel.load() ? 0 : wanted;
            return rgba;
        }
    };
    std::shared_ptr<VideoStill> still;

    // The whole video prepared with the settings shown, played behind the
    // settings once it is; Prepare keeps it when its settings are still these.
    struct Bake {
        motion::RasterSettings settings;
        std::atomic<bool> cancel {false};
        std::atomic<double> progress {0};
    };
    std::shared_ptr<Bake> bake;
    std::shared_ptr<const motion::PreparedPointFrames> baked;
    std::shared_ptr<const juce::MemoryBlock> bakedArchive;
    std::optional<motion::RasterSettings> videoShown;
    motion::RasterSettings bakedSettings;
    static bool same(const motion::RasterSettings& a, const motion::RasterSettings& b) {
        return a.mode == b.mode && a.shading == b.shading && a.threshold == b.threshold && a.invert == b.invert && a.resolution == b.resolution
            && a.videoFrameRate == b.videoFrameRate && a.pointsPerFrame == b.pointsPerFrame;
    }
    void startBake() {
        bakeDelay.stopTimer();
        if (still == nullptr) { return; }
        auto job = std::make_shared<Bake>();
        job->settings = settings;
        bake = job;
        bakeProgress.startTimerHz(8);
        showProgress();
        bakes.addJob([job, still = still, owner = juce::Component::SafePointer<MotionRasterSettingsPanel>(this)] {
            juce::MemoryBlock data = still->bytes;
            if (data.getSize() == 0) { still->video.loadFileAsData(data); }
            auto prepared = motion::VideoSourcePreparer::prepare(data, still->ffmpeg, job->settings, &job->cancel, &job->progress);
            if (job->cancel.load()) { return; }
            // Played as frames traced here, a share of them for a long video.
            motion::ui::SourcePreview::Result result;
            if (prepared) {
                const auto source = motion::PreparedSource(prepared.source);
                const auto count = source.frameCount();
                const auto shown = std::min<std::size_t>(count, 240);
                for (std::size_t index = 0; index < shown && !job->cancel.load(); ++index) {
                    // Every point, as the still is traced: fewer join the gaps between strokes.
                    result.frames.push_back(motion::ui::traceSource(source, index * count / shown, static_cast<int>(job->settings.pointsPerFrame)));
                }
                result.framesPerSecond = job->settings.videoFrameRate;
                result.frameStep = static_cast<double>(count) / static_cast<double>(std::max<std::size_t>(shown, 1));
                result.caption = juce::String(static_cast<juce::int64>(count)) + " frames" + motion::style::dot()
                    + juce::String(static_cast<double>(count) / job->settings.videoFrameRate, 1) + " s";
            } else {
                result.error = juce::String(prepared.error);
            }
            juce::MessageManager::callAsync([owner, job, frames = prepared.source, result = std::move(result)]() mutable {
                if (owner == nullptr || owner->bake != job) { return; }
                owner->bake = nullptr;
                owner->bakeProgress.stopTimer();
                owner->preview.setCaption({});
                if (frames != nullptr) {
                    owner->baked = frames;
                    owner->bakedSettings = job->settings;
                }
                owner->preview.show(std::move(result));
            });
            // Then kept as the cache it will be saved with, while it plays.
            if (prepared.source == nullptr) { return; }
            auto archive = motion::BakedSourceArchive::encode(*prepared.source);
            if (!archive || job->cancel.load()) { return; }
            juce::MessageManager::callAsync([owner, job, archive = std::make_shared<const juce::MemoryBlock>(std::move(archive.data))] {
                if (owner == nullptr || owner->bake != nullptr || !same(owner->bakedSettings, job->settings)) { return; }
                owner->bakedArchive = archive;
            });
        });
    }
    void showProgress() {
        if (bake == nullptr) { return; }
        preview.setCaption("Preparing the whole video" + motion::style::dot() + juce::String(juce::roundToInt(bake->progress.load() * 100)) + "%");
    }
    juce::TimedCallback bakeDelay {[this] { startBake(); }};
    juce::TimedCallback bakeProgress {[this] { showProgress(); }};
    juce::ThreadPool bakes {1};
    void selectSamples(std::size_t count) {
        const auto found = std::find(strides.begin(), strides.end(), count);
        if (found != strides.end()) { samples.setSelectedId(static_cast<int>(std::distance(strides.begin(), found) + 1), juce::dontSendNotification); }
    }
    void selectResolution(int resolution) {
        const auto found = std::find(resolutions.begin(), resolutions.end(), resolution);
        if (found != resolutions.end()) { detail.setSelectedId(static_cast<int>(std::distance(resolutions.begin(), found) + 1), juce::dontSendNotification); }
    }
    void refresh() {
        auto next = settings;
        const auto detailIndex = detail.getSelectedId() - 1;
        const auto sampleIndex = samples.getSelectedId() - 1;
        juce::String message;
        if (detailIndex < 0 || sampleIndex < 0 || static_cast<std::size_t>(detailIndex) >= resolutions.size() || static_cast<std::size_t>(sampleIndex) >= strides.size()) {
            message = "Choose an image detail and samples per frame.";
        } else {
            next.mode = mode.getSelected() == 0 ? motion::RasterSettings::Mode::contours : mode.getSelected() == 1 ? motion::RasterSettings::Mode::scanlines : motion::RasterSettings::Mode::hilbert;
            next.resolution = resolutions[static_cast<std::size_t>(detailIndex)];
            next.pointsPerFrame = strides[static_cast<std::size_t>(sampleIndex)];
            next.invert = invert.getToggleState();
            next.shading = shading.getSelected() == 1 ? motion::RasterSettings::Shading::colour : motion::RasterSettings::Shading::density;
            if (thresholdEdited) { next.threshold = threshold.getValue() / 100.0; }
            message = juce::String(next.validate());
        }
        // Shading is the Hilbert curve's alone.
        const auto hilbert = mode.getSelected() == 2;
        shading.setEnabled(hilbert);
        shadingLabel.setAlpha(hilbert ? 1.0f : .4f);
        valid = message.isEmpty();
        error.setText(message, juce::dontSendNotification);
        primary.setEnabled(valid && !submitted);
        if (!valid) { return; }
        settings = next;
        if (!hasPreview()) { return; }
        if (live.has_value()) {
            // Each change applied is an undo step, so opening the sheet, or
            // choosing what is already applied, applies nothing.
            if (live->onChange && !same(liveApplied, settings)) {
                liveApplied = settings;
                live->onChange(settings);
            }
            recordError.clear();
            return;
        }
        if (isVideo) {
            // Settings as they were (Prepare refreshes too) leave the frame,
            // and the whole video prepared or preparing, as they are.
            if (videoShown.has_value() && same(*videoShown, settings)) { return; }
            videoShown = settings;
            preview.request([still = still, chosen = settings](const std::atomic<bool>& cancel) {
                motion::ui::SourcePreview::Result result;
                const auto& rgba = still->at(chosen.resolution, cancel);
                if (rgba.empty()) {
                    if (!cancel.load()) { result.error = "Couldn't read a frame of this video."; }
                    return result;
                }
                auto beam = motion::RasterBeamBuilder::build(rgba.data(), rgba.size(), chosen.resolution, chosen.resolution, chosen, &cancel);
                if (!beam) {
                    result.error = juce::String(beam.error);
                    return result;
                }
                const auto frame = motion::PreparedPointFrames::create(chosen.videoFrameRate, 1, chosen.pointsPerFrame, std::move(beam.points));
                if (!frame) {
                    result.error = juce::String(frame.error);
                    return result;
                }
                result.path = motion::ui::traceSource(motion::PreparedSource(frame.source), 0, static_cast<int>(chosen.pointsPerFrame));
                result.caption = "One frame" + motion::style::dot() + juce::String(static_cast<juce::int64>(chosen.pointsPerFrame)) + " points";
                return result;
            });
            // Then the whole video, a moment after the settings settle.
            if (bake != nullptr) { bake->cancel.store(true); }
            bake = nullptr;
            baked = nullptr;
            bakedArchive = nullptr;
            bakeDelay.startTimer(500);
            return;
        }
        preview.request([image = data, chosen = settings](const std::atomic<bool>& cancel) {
            motion::ui::SourcePreview::Result result;
            const auto prepared = motion::RasterSourcePreparer::prepare(image.getData(), image.getSize(), chosen, &cancel);
            if (!prepared) {
                result.error = juce::String(prepared.error);
                return result;
            }
            result.path = motion::ui::traceSource(*prepared.source, 0, static_cast<int>(chosen.pointsPerFrame));
            const auto frames = static_cast<int>(prepared.source->frameCount());
            result.caption = juce::String(static_cast<juce::int64>(chosen.pointsPerFrame)) + " points" + (frames > 1 ? motion::style::dot() + juce::String(frames) + " frames" : juce::String());
            return result;
        });
    }

    motion::RasterSettings settings;
    bool isVideo = false;
    juce::MemoryBlock data;
    bool valid = false, submitted = false, thresholdEdited = false, detailEdited = false, samplesEdited = false;
    std::vector<int> resolutions;
    std::vector<std::size_t> strides;
    motion::ui::SegmentedControl mode {"Image trace mode", {"Outlines", "Scanlines", "Hilbert"}};
    motion::ui::SegmentedControl shading {"Image shading", {"Density", "Brightness"}};
    motion::ui::ScrubField threshold, frameRate;
    juce::ComboBox detail, samples;
    motion::ui::Switch invert {"Invert image"};
    motion::ui::SourcePreview preview;
    juce::Label modeLabel {"Image mode caption", "Trace"}, shadingLabel {"Image shading caption", "Shading"}, thresholdLabel {"Image threshold caption", "Threshold"};
    juce::Label detailLabel {"Image detail caption", "Detail"}, samplesLabel {"Image samples caption", "Samples per frame"};
    juce::Label frameRateLabel {"Video frame rate caption", "Frame rate"}, invertLabel {"Invert caption", "Invert"};
    juce::Label error;
};
