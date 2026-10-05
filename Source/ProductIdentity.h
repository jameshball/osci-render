#pragma once

namespace osci {
struct ProductIdentity {
    const char* name;
    const char* slug;
    bool hostedServicesAvailable;
};

inline constexpr ProductIdentity currentProduct() {
#if defined(OSCI_MOTION)
    return { "osci-motion", "osci-motion", false };
#elif defined(SOSCI)
    return { "sosci", "sosci", true };
#else
    return { "osci-render", "osci-render", true };
#endif
}
}
