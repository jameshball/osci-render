#pragma once

namespace osci {
inline constexpr const char* currentProductSlug() {
#if defined(OSCI_MOTION)
    return "osci-motion";
#elif defined(SOSCI)
    return "sosci";
#else
    return "osci-render";
#endif
}
}
