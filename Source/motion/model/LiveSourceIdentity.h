#pragma once

namespace motion {
// Runtime identity is shared by an asset and its prepared clips, never derived
// from reusable document IDs. Replacing a project/source creates a fresh key.
struct LiveSourceIdentity final {};
}
