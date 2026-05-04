#include "AboutComponent.h"

juce::Point<int> AboutComponent::preferredSize(const Info& info) {
    float h = kPad + kLogoH + 6.0f + 24.0f + 18.0f + 16.0f + kGap;
    h += kCardPad + 20.0f + (float)info.credits.size() * kRowH + 18.0f + kCardPad;
    h += kGap;
    h += kCardPad + 16.0f + (info.blenderPort > 0 ? 16.0f : 0.0f) + kCardPad;
    h += kGap + kBtnH + kPad;
    return { 520, (int)std::ceil(h) };
}

std::unique_ptr<osci::OverlayComponent> AboutComponent::createOverlay(const Info& info, juce::String closeButtonSvg) {
    auto content = std::make_unique<AboutComponent>(info);
    return std::make_unique<osci::ComponentOverlay>(std::move(content),
                                                    std::move(closeButtonSvg),
                                                    juce::String(),
                                                    preferredSize(info),
                                                    false);
}

AboutComponent::AboutComponent(const Info& info) : info(info) {
    logo = juce::ImageFileFormat::loadFrom(info.imageData, info.imageSize);
    logoComponent.setImage(logo);
    addAndMakeVisible(logoComponent);

    auto setupBtn = [this](juce::TextButton& btn, const juce::String& text, const juce::String& url, juce::Colour tint) {
        btn.setButtonText(text);
        btn.setColour(juce::TextButton::buttonColourId, tint.withAlpha(0.15f));
        btn.setColour(juce::TextButton::buttonOnColourId, tint.withAlpha(0.25f));
        btn.setColour(juce::TextButton::textColourOnId, tint);
        btn.setColour(juce::TextButton::textColourOffId, tint);
        btn.onClick = [url]() { juce::URL(url).launchInDefaultBrowser(); };
        addAndMakeVisible(btn);
    };

    // Brighten Discord brand colour so its hover/contrast feels similar to the accent buttons.
    const juce::Colour discordColour = juce::Colour::fromRGB(0x58, 0x65, 0xF2).brighter(0.4f);
    setupBtn(websiteBtn, "Website", info.websiteUrl, osci::Colours::accentColor());
    setupBtn(discordBtn, "Join Discord", "https://discord.gg/ekjpQvT68C", discordColour);
    setupBtn(issuesBtn, "Report Issue", info.githubUrl + "/issues", osci::Colours::accentColor());

    auto sz = preferredSize(info);
    setSize(sz.x, sz.y);
}

void AboutComponent::paintCard(juce::Graphics& g, juce::Rectangle<float> area) const {
    g.setColour(osci::Colours::darkerer());
    g.fillRoundedRectangle(area, kCardRadius);
}

void AboutComponent::paint(juce::Graphics& g) {
    auto area = getLocalBounds().toFloat().reduced(kPad, 0.0f);
    area.removeFromTop(kPad);

    // Logo is positioned by resized()
    area.removeFromTop(kLogoH);
    area.removeFromTop(6.0f);

    // Product name
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.setColour(juce::Colours::white);
    g.drawText(info.productName, area.removeFromTop(24.0f), juce::Justification::centred);

    // "by Company"
    g.setFont(juce::FontOptions(13.0f));
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawText("by " + info.companyName, area.removeFromTop(18.0f), juce::Justification::centred);

    // Version / build status
    juce::String status = "Version " + info.versionString + "  \xc2\xb7  ";
    status += info.isPremium ? "Premium" : "Free";
    if (info.betaUpdatesEnabled)
        status += "  \xc2\xb7  Beta updates enabled";
    if (betaUnlockMessage.isNotEmpty())
        status = betaUnlockMessage;
    g.setFont(juce::FontOptions(12.0f));
    g.setColour(betaUnlockMessage.isNotEmpty() ? osci::Colours::accentColor().brighter(0.15f)
                                                : juce::Colours::white.withAlpha(0.65f));
    versionStatusBounds = area.removeFromTop(16.0f);
    g.drawText(status, versionStatusBounds, juce::Justification::centred);

    area.removeFromTop(kGap);

    // --- Credits card ---
    {
        float cardH = kCardPad + 20.0f + (float)info.credits.size() * kRowH + 18.0f + kCardPad;
        auto card = area.removeFromTop(cardH);
        paintCard(g, card);
        auto inner = card.reduced(kCardPad + 4.0f, kCardPad);

        // Title
        g.setFont(juce::FontOptions(11.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.75f));
        g.drawText("CONTRIBUTORS", inner.removeFromTop(20.0f), juce::Justification::centredLeft);

        // Entries
        for (auto& c : info.credits) {
            auto nameRow = inner.removeFromTop(18.0f);
            g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
            g.setColour(juce::Colours::white);
            g.drawText(c.name, nameRow, juce::Justification::centredLeft);

            auto descRow = inner.removeFromTop(16.0f);
            g.setFont(juce::FontOptions(12.0f));
            g.setColour(juce::Colours::white.withAlpha(0.7f));
            g.drawText(c.contribution, descRow, juce::Justification::centredLeft);
        }

        g.setFont(juce::FontOptions(11.0f, juce::Font::italic));
        g.setColour(juce::Colours::white.withAlpha(0.6f));
        g.drawText("...and all the community!", inner.removeFromTop(18.0f), juce::Justification::centredLeft);
    }

    area.removeFromTop(kGap);

    // --- System info card ---
    {
        float cardH = kCardPad + 16.0f + (info.blenderPort > 0 ? 16.0f : 0.0f) + kCardPad;
        auto card = area.removeFromTop(cardH);
        paintCard(g, card);
        auto inner = card.reduced(kCardPad + 4.0f, kCardPad);

        g.setFont(juce::FontOptions(12.0f));
        g.setColour(juce::Colours::white.withAlpha(0.65f));

        juce::String buildInfo = juce::String("Built ") + __DATE__ + "  \xc2\xb7  " + juce::SystemStats::getJUCEVersion();
        g.drawText(buildInfo, inner.removeFromTop(16.0f), juce::Justification::centred);

        if (info.blenderPort > 0) {
            g.drawText("Blender Port: " + juce::String(info.blenderPort),
                       inner.removeFromTop(16.0f), juce::Justification::centred);
        }
    }
}

void AboutComponent::resized() {
    auto area = getLocalBounds().reduced((int)kPad, (int)kPad);
    logoComponent.setBounds(area.removeFromTop((int)kLogoH));

    auto btnRow = getLocalBounds().reduced((int)kPad).removeFromBottom((int)kBtnH);
    int totalW = btnRow.getWidth();
    int btnW = (int)((totalW - kBtnGap * 2.0f) / 3.0f);
    websiteBtn.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft((int)kBtnGap);
    discordBtn.setBounds(btnRow.removeFromLeft(btnW));
    btnRow.removeFromLeft((int)kBtnGap);
    issuesBtn.setBounds(btnRow);
}

void AboutComponent::mouseUp(const juce::MouseEvent& event) {
    if (!versionStatusBounds.contains(event.position))
        return;

    betaUnlockClicks++;
    const int remaining = 5 - betaUnlockClicks;
    if (remaining > 0) {
        const auto action = info.betaUpdatesEnabled ? "return to stable updates" : "enable beta updates";
        showTemporaryBetaMessage(juce::String(remaining) + (remaining == 1 ? " more click" : " more clicks") + " to " + action);
    } else {
        info.betaUpdatesEnabled = !info.betaUpdatesEnabled;
        betaUnlockClicks = 0;
        showTemporaryBetaMessage(info.betaUpdatesEnabled ? "Beta updates enabled" : "Stable updates enabled");
        if (info.onBetaUpdatesChanged)
            info.onBetaUpdatesChanged(info.betaUpdatesEnabled);
    }
}

void AboutComponent::showTemporaryBetaMessage(const juce::String& message) {
    betaUnlockMessage = message;
    repaint();

    juce::Component::SafePointer<AboutComponent> safeThis(this);
    juce::Timer::callAfterDelay(2000, [safeThis]
    {
        if (safeThis == nullptr)
            return;

        safeThis->betaUnlockMessage.clear();
        safeThis->repaint();
    });
}
