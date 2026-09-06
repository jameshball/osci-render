#include "FramePresenter.h"

#if !JUCE_MAC
std::unique_ptr<FramePresenter> FramePresenter::create(juce::Component&, juce::OpenGLContext&) {
    return {};
}
#endif
