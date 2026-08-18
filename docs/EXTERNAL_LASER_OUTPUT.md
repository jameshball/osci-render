# External laser-output contract

osci-render can send its live post-effects point stream to a separately
installed output application. The integration is an optional runtime peer: the
public osci-render build never links, loads, configures, or arms private laser
product code.

## Transport

- Protocol: IDN continuous graphic mode over UDP.
- Default endpoint: `127.0.0.1:7255`.
- Point descriptor: high-resolution X, Y, red, green, and blue.
- X and Y are normalised semantic coordinates in `[-1, 1]`.
- RGB channels are unsigned 16-bit values. IDN intensity is always full scale,
  so the receiving application applies intensity exactly once.
- The stream point rate is the active osci-render processing rate, bounded by
  the sender and declared when the IDN session starts.
- ILDA files are interchange artifacts and are not used for live IPC.

The implementation uses the public MIT `laser-dac-c` ABI with only its IDN
producer feature enabled. The bridge pins its Rust toolchain, Cargo lockfile,
and upstream backend revision.

macOS release builds link a deterministic arm64/x86_64 bridge archive. Windows
builds link the x64 MSVC archive, while Linux builds use the runner's native
x86_64 or arm64 archive. The public build workflow creates these artifacts
before Projucer exports and links osci-render.

## Lifecycle

The user must explicitly launch the peer and explicitly start streaming. State
restoration never launches a process or starts a stream. Transport stop,
processor suspension, queue overflow, sender failure, or explicit Stop closes
the IDN session. Reconnection is deliberate and never requests remote arming.

The receiver owns scanner conditioning, profiles, device access, licensing,
watchdogs, blackout policy, and all arming decisions. osci-render exposes no
hardware-control API.

## Installation discovery

- macOS: `/Applications/osci-laser.app` or the user Applications directory.
- Windows: the standard `App Paths` product registration, with the default
  Program Files location as a fallback.
- Linux: `/usr/bin`, `/usr/local/bin`, `/opt/osci-laser`, or `~/.local/bin`.

`OSCI_LASER_APP_PATH` overrides discovery for development and hermetic tests.
It is not saved in projects.

## Network boundary

The receiving application binds loopback by default. LAN reception is an
explicit local receiver preference with an exact source-IP allowlist. Only one
IDN controller is accepted at a time. Source timeout requests receiver-side
blackout and disarm. Software transport is not represented as certified laser
safety equipment.

## Compatibility

Consumers negotiate `LDC_ABI_VERSION` and populate every public structure's
`struct_size` and `abi_version`. New fields may only be appended. Ownership,
callback threads, error handling, platform linker requirements, and custom
backend rules are documented by `modules/laser_dac_c/docs/ABI.md`.
