#pragma once

namespace motion {
struct BlenderSourceSettings {
    int port = 51677;
    bool freezeOnDisconnect = true;
    bool valid() const { return port >= 51600 && port <= 51699; }
    bool operator==(const BlenderSourceSettings&) const = default;
};
}
