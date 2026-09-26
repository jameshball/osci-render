#pragma once

namespace osci {
struct ProductIdentity {
    const char* name;
    const char* slug;
    const char* projectExtension;
    bool hostedServicesAvailable;
};

inline constexpr ProductIdentity currentProduct() {
#if defined(OSCI_MOTION)
    return { "osci-motion", "osci-motion", "osci-motion", false };
#elif defined(SOSCI)
    return { "sosci", "sosci", "sosci", true };
#else
    return { "osci-render", "osci-render", "osci", true };
#endif
}
}
