#pragma once

#include <JuceHeader.h>
#include <cmath>
#include <regex>

namespace osci::video
{
struct FFmpegMediaInfo
{
    int width = 0;
    int height = 0;
    double frameRate = 0.0;
};

namespace detail
{
inline juce::String runProcess(const juce::StringArray& command)
{
    juce::ChildProcess process;
    if (!process.start(command, juce::ChildProcess::wantStdOut | juce::ChildProcess::wantStdErr))
        return {};

    return process.readAllProcessOutput();
}

inline double parsePositiveRate(const std::string& text)
{
    try
    {
        const auto slash = text.find('/');
        if (slash != std::string::npos)
        {
            const double numerator = std::stod(text.substr(0, slash));
            const double denominator = std::stod(text.substr(slash + 1));
            const double rate = denominator != 0.0 ? numerator / denominator : 0.0;
            return std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
        }

        const double rate = std::stod(text);
        return std::isfinite(rate) && rate > 0.0 ? rate : 0.0;
    }
    catch (...)
    {
        return 0.0;
    }
}

inline FFmpegMediaInfo parseFFmpegInputOutput(const juce::String& output)
{
    FFmpegMediaInfo info;
    juce::StringArray lines;
    lines.addLines(output);

    const std::regex resolutionRegex(R"((?:^|[,\s])(\d{2,5})x(\d{2,5})(?:[,\s\[]|$))");
    const std::regex fpsRegex(R"((\d+(?:\.\d+)?(?:/\d+(?:\.\d+)?)?)\s*fps\b)");
    const std::regex tbrRegex(R"((\d+(?:\.\d+)?(?:/\d+(?:\.\d+)?)?)\s*tbr\b)");

    for (const auto& line : lines)
    {
        if (!line.containsIgnoreCase("Video:"))
            continue;

        const auto stdLine = line.toStdString();
        std::smatch match;

        if (std::regex_search(stdLine, match, resolutionRegex) && match.size() == 3)
        {
            info.width = juce::String(match[1].str()).getIntValue();
            info.height = juce::String(match[2].str()).getIntValue();
        }

        if (std::regex_search(stdLine, match, fpsRegex) && match.size() == 2)
            info.frameRate = parsePositiveRate(match[1].str());
        else if (std::regex_search(stdLine, match, tbrRegex) && match.size() == 2)
            info.frameRate = parsePositiveRate(match[1].str());

        if (info.width > 0 && info.height > 0)
            return info;
    }

    return info;
}
}

inline FFmpegMediaInfo probeFFmpegMediaInfo(const juce::File& ffmpegFile, const juce::File& mediaFile)
{
    juce::StringArray ffmpegCommand;
    ffmpegCommand.add(ffmpegFile.getFullPathName());
    ffmpegCommand.add("-hide_banner");
    ffmpegCommand.add("-i");
    ffmpegCommand.add(mediaFile.getFullPathName());

    return detail::parseFFmpegInputOutput(detail::runProcess(ffmpegCommand));
}
}