# Licensing & Auto-Update System — Design Doc

Status: **Partially implemented (backend/admin/release pipeline live; JUCE licensing module + plugin update prompt live; installer app pending)**
Author: GitHub Copilot, captured for james
Last updated: 2026-05-03

This document captures the agreed-on design for:

1. A small, rarely-updated **JUCE installer app** that prompts for a license key and downloads the latest osci-render / sosci.
2. License activation against **Gumroad + Payhip** via the existing `osci-render-api` Flask backend.
3. **Auto-update** support inside both the installer and the plugins themselves.
4. Shared C++ code between the installer, `osci-render`, and `sosci` via a new JUCE module.

A local clone of the existing API lives at `./osci-render-api/` (gitignored) for ease of editing during implementation.

---

## 0. Implementation status as of 2026-05-03

### Done
- [x] API is migrated to Fly.io + Postgres-backed local/test/CI workflows.
- [x] Payhip product fields, Payhip verification, and Payhip webhooks exist in the backend.
- [x] `/api/license/activate` mints Ed25519-signed activation tokens.
- [x] `/api/version/latest` and `/api/version/download-url` exist and query real `Version`/`VersionAsset` rows.
- [x] R2 presigned upload/download plumbing exists for release artifacts.
- [x] Admin UI can manage products, versions, version assets, release tracks, imports, analytics, and R2 browsing.
- [x] `Version` now represents a product + semver + release track; `VersionAsset` represents platform/variant/artifact rows.
- [x] CI publishing exists via `ci/publish_release.py` and `/api/admin/version/publish`.
- [x] osci-render CI always publishes release assets as `alpha`; the old `--release` bump flag is gone.
- [x] Local admin runs on `http://127.0.0.1:5001/admin/` with real Google OAuth and no localhost login bypass.
- [x] Generic mutating HTTP audit logging exists through `AuditLog` and `AuditRecorder`.
- [x] `modules/osci_licensing/` exists as a public GPLv3 submodule and is wired into osci-render, sosci, and the test target.
- [x] The JUCE module has a production Ed25519 verifier via vendored Monocypher plus tests for the real verifier path.
- [x] osci-render and sosci have a shared License and Updates UI for activation, release-track selection, update checks, verified downloads, and installer handoff.
- [x] The shared processor owns a per-product `LicenseManager` and loads cached license tokens on startup.
- [x] Client-side 30-day token expiry plus 14-day offline grace is implemented in the JUCE module.
- [x] Premium artifact presigned download URLs require a valid signed premium activation token.
- [x] osci-render and sosci check for updates after editor launch and show a top-right update prompt when a newer version is available.
- [x] Plugin update prompts support 48-hour dismissal per version.
- [x] Beta updates are hidden behind a 5-click About-version toggle and persist in global settings.
- [x] The main editor shows a top-right `Beta updates` indicator button while beta mode is enabled; it opens the account/update panel.
- [x] The account/update panel can switch beta mode back to stable.
- [x] Premium builds without a valid premium license open a non-dismissible activation overlay on launch.
- [x] osci-render premium builds without a valid premium license can install the latest stable free artifact instead of continuing in-place; sosci has no free fallback.
- [x] Overlay title/close chrome is centralized in `OverlayComponent` and reused by account, premium splash, and Lua documentation overlays.
- [x] `OverlayComponent` uses an `SvgButton` (`close_svg`) for the close button; Escape key dismisses; clicking outside the overlay while a `TextEditor` is focused is ignored.
- [x] `DownloadProgressComponent` (`Source/components/DownloadProgressComponent.h`) is a generic reusable widget wrapping `juce::ProgressBar` + a status label. It is thread-safe (marshals to the message thread) and is used by both `DownloaderComponent` (FFmpeg) and `LicenseAndUpdatesComponent` (plugin updates). The license overlay uses a 48 px tall bar; the FFmpeg downloader uses 28 px.
- [x] JUCE backend client API logging is opt-in via `OSCI_LICENSING_ENABLE_API_LOGGING=1`; default builds do not log API traffic, and the debug logger redacts known secrets.
- [x] Version endpoints resolve public product slugs such as `osci-render` and `sosci`, fixing the plugin-side `unknown product` check-for-update failure.
- [x] The product-slug resolver fix has been deployed to Fly and verified with `product=osci-render` returning HTTP 200.
- [x] Alpha updates are hidden from plugin UI; alpha is available only through authenticated admin/backend flows.
- [x] `VersionAsset.size_bytes` column added to backend (nullable `BigInteger`); `ci/publish_release.py` sends `stat().st_size`; `/api/version/latest` manifest includes `size_bytes`; client reads it in `BackendClient` as a download-cap hint.
- [x] Production backend/release public key values are embedded in the app build definitions.
- [x] Admin deletion of `Version`/`VersionAsset` metadata now flushes the DB delete and deletes eligible R2 objects before commit. If R2 fails before any object is deleted, metadata rolls back; if R2 fails after partial object deletion, metadata commits to avoid restoring rows that point at missing binaries.
- [x] The R2 browser no longer exposes direct object deletion and `/api/admin/r2/delete` has been removed; release artifact deletion must flow through `Version`/`VersionAsset` metadata deletion.
- [x] `osci::SettingsStore` lives in `modules/osci_render_core` and wraps JUCE `PropertiesFile` for product globals, standalone app settings, and shared licensing settings.
- [x] License tokens, update preferences, and pending-install markers use the shared `osci-licensing.settings` store with product-scoped keys.
- [x] `UpdateSettings` now persists only `releaseTrack`; beta mode is derived from `releaseTrack != stable` rather than a duplicate `betaEnabled` flag.
- [x] Pending-install handoff is implemented: installers write a marker before launch, startup resolves markers by running version, and unresolved markers show a retry/redownload prompt.
- [x] DAW/test-host detection is implemented as a warning-only process scan on macOS/Linux/Windows paths; install launch flows include detected host names but never close or signal host processes.
- [x] Update prompt and account/update panel share installer confirmation, DAW warning copy, pending-marker write/cleanup, and launch failure handling via `InstallFlowHelpers`.

### Changed during implementation
- Release channels became **release tracks**: `alpha`, `beta`, and `stable`. `alpha` metadata/downloads require an admin session; `beta` and `stable` are public.
- The first CI-backed release flow is intentionally **alpha-only** so we can test end-to-end publishing before promoting anything to `stable`.
- Product binaries keep separate compile-time `free`/`premium` variants. Licensing authorizes premium downloads/updates, but it does not unlock premium features inside a free binary at runtime.
- R2 object keys intentionally do **not** include `release_track`; promotion is mutable metadata, while R2 keys are based on product, semver, variant, platform, and filename.
- A free build with a valid premium activation token can target/download the separate premium artifact; it still does not unlock premium features in-place.
- Local/test development is Postgres-only; SQLite compatibility was intentionally removed.
- Admin OAuth now behaves the same on localhost as production. To test locally, Google OAuth credentials must allow `http://127.0.0.1:5001/auth`.

### Still to do
- [ ] Confirm the remaining private-key backup names and rotation runbook outside source control.
- [x] Deploy the `size_bytes` migration/API change to production.
- [ ] Continue polishing the account/update panel visual design; current pass is compact, generic-overlay based, and uses one `Download and install` action.
- [ ] Finish installer completion/product install UX in the dedicated installer app; plugin-side handoff now uses pending markers rather than trying to observe installer completion.
- [ ] Create `osci-installer.jucer` and the installer UX.
- [x] Decide promotion workflow: builds publish as `alpha`, then admin promotes to `beta`/`stable` in the admin UI.

### Next ordered TODOs
- [x] 1. Deploy backend `size_bytes` change.
  - [x] Deploy API to Fly.
  - [x] Smoke check production `/api/version/latest` returns `size_bytes`.
- [ ] 2. Confirm production key/secret inventory.
  - [x] Verify embedded backend public key matches Fly `ED25519_TOKEN_PRIVATE_KEY`.
  - [x] Verify embedded release public key matches Fly `RELEASE_SIGNING_PUBLIC_KEY`.
  - [x] Verify current production stable artifact signature with the embedded release public key.
  - [x] Confirm GitHub `PUBLISH_API_TOKEN` and `RELEASE_SIGNING_PRIVATE_KEY` secret names exist.
  - [ ] Confirm 1Password backup item names and rotation steps are documented outside source control.
  - [ ] Remove stale/unneeded Fly `ED25519_RELEASE_PRIVATE_KEY` if confirmed unused.
- [x] 3. Implement installer handoff groundwork.
  - [x] Add pending-install marker helper in `modules/osci_licensing`.
  - [x] Write marker before `InstallerLauncher::launchAndExitHost`.
  - [x] On next startup, detect resolved/unresolved markers and show success/retry prompts.
- [x] 4. Add DAW-running detection.
  - [x] Implement best-effort process scan.
  - [x] Warn only; do not close host processes.
  - [x] Integrate before launch in both update prompt and account/update panel.
- [ ] 5. Create `osci-installer.jucer`.
  - [ ] Reuse licensing/update primitives.
  - [ ] Implement minimal welcome → key/free choice → install flow.
  - [ ] Share `osci-licensing.settings` with osci-render and sosci.
- [ ] 6. Harden installer/update edge cases before public release.
  - [ ] Move DAW process scanning off the message thread or make `readProcessList()` non-blocking.
  - [ ] Smoke test pending-install success/failure/retry prompts with real installers on macOS and Windows.
  - [ ] Decide whether plugin-hosted update launches should only warn/retry, or also guide users to reopen their DAW after installation.

---

## 1. Goals & non-goals

### Goals
- Single small installer artifact (one `.dmg`, one `.exe`, one `.AppImage`) that we publish once and rarely update.
- Installer asks for license key on first run (or "continue free") and downloads the latest osci-render and/or sosci appropriate for the OS/arch.
- osci-render and sosci both check for updates on launch and can perform the upgrade themselves (using the same shared code as the installer).
- License verification works for both **Gumroad** and **Payhip**, with a **single client-side flow** ("paste your key, we figure out which store").
- "Online once, then cached" licensing — re-validate occasionally, no offline grace required.
- No machine binding (use Gumroad's `uses` count and Payhip's `uses` field as a soft signal only).
- Premium feature gating remains compile-time via separate free/premium builds. Licensing controls activation, premium downloads, and update eligibility, not in-place feature unlocks.

### Non-goals (for v1)
- Hard hardware locking, dongle support, floating licenses.
- Trial periods / time-limited demos.
- LDAP/SSO/team licenses.
- Forced delta updates (we ship full installers).

---

## 2. High-level architecture

```
┌────────────────────────┐    HTTPS    ┌──────────────────────────┐
│  osci-installer (app)  ├────────────▶│  osci-render-api (Flask) │
│  osci-render (plugin)  │   verify    │     (Cloudflare-fronted) │
│  sosci (plugin)        │    update   │                          │
└─────────┬──────────────┘             │   ┌──────────────────┐   │
          │ uses                       │   │  /api/license/*  │───┼──▶ Gumroad
          ▼                            │   │  /api/version/*  │───┼──▶ Payhip
┌────────────────────────┐             │   │  /api/download/* │   │
│ modules/osci_licensing │             │   └──────────┬───────┘   │
│   (shared JUCE module) │             └──────────────┼───────────┘
└────────────────────────┘                            │
                                                      ▼  presigned URL
                                            ┌──────────────────┐
                                            │ Cloudflare R2    │
                                            │ (release assets) │
                                            └──────────────────┘
```

---

## 3. Decisions (already approved)

| # | Decision | Choice |
|---|----------|--------|
| 1 | Installer style | **Hybrid**: native installers (.pkg / .exe) on macOS + Windows; direct file copy on Linux |
| 2 | Updater library | **Hand-rolled** in JUCE — single codebase, license-gated downloads, no Sparkle/WinSparkle dependency |
| 3 | Plugin self-check | Plugins check **and** can perform the upgrade; installer does the same |
| 4 | Artifact hosting | **Cloudflare R2** with backend-issued short-lived presigned URLs |
| 5 | App + DB hosting | **Fly.io** (Flask app) + **Neon** (Postgres) + **Cloudflare R2** (artifacts) — see §13 |
| 6 | Domain | `api.osci-render.com` (Cloudflare DNS, already in use by current Heroku deploy) |
| 7 | Hardware/seat lock | None — soft signal only via `uses` counter |
| 8 | Backwards-compat | Don't worry about it — fresh start |
| 9 | Free-tier UX | **Silent** — no nag (matches today's `OSCI_PREMIUM=0` behaviour) |
| 10 | sosci licensing | **Separate paid keys** for sosci, independent of osci-render |
| 11 | Token lifetime | **30 days online + 14 days offline grace** |
| 12 | Migration of existing premium users | **Hard cutover** — next premium release requires entering a key on first launch |
| 13 | Ed25519 keys | **Generate now** as part of backend step 1; stash in 1Password + Fly secrets |
| 14 | Linux format | **AppImage only** for standalone; `.vst3`/`.lv2` shipped as tar.gz |
| 15 | Release tracks | **Changed during implementation** — use `alpha`, `beta`, and `stable`; CI publishes osci-render as `alpha` only for now |
| 16 | Release promotion | **Admin UI promotion** — CI publishes `alpha`; operator changes release track to `beta`/`stable` after testing |
| 17 | Premium/free rollout | **Keep separate free/premium artifacts**; do not replace compile-time `OSCI_PREMIUM` with runtime feature gating |
| 18 | Payhip secret storage | **Keep current storage** — secrets remain server-side and hidden from admin API responses; no field-level encryption for now |
| 19 | Audit visibility | **Add read-only admin UI** for audit logs |

---

## 4. Storefront license APIs (verified contracts)

### 4.1 Gumroad — already integrated server-side

Endpoint: `POST https://api.gumroad.com/v2/licenses/verify`
- Form params: `product_id`, `license_key`, `increment_uses_count` (default `true`).
- No OAuth required for verify. Other endpoints (`enable`/`disable`/`decrement_uses_count`/`rotate`) require an `access_token`.
- Response includes `success`, `uses`, and a full `purchase` object with `email`, `product_id`, `license_disabled`, `refunded`, `disputed`, `chargebacked`, `subscription_cancelled_at`, `sale_timestamp`, `recurrence`, `variants`.
- Source: <https://gumroad.com/api#licenses>.

Known osci-render product IDs (in the Flask app already):
- osci-render premium: `wHBxF-jb7oyeVNPRzKDorA==`
- sosci: `Hsr9C58_YhTxYP0MNvsIow==`

### 4.2 Payhip — confirmed via locally-saved `API Reference - Payhip.html`

Base URL: `https://payhip.com/api/v2`
Auth: per-product secret in the `product-secret-key` HTTP header. **Each product has its own secret**, which means our backend needs a `Product.payhip_product_secret` column alongside `Product.payhip_product_link`.

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/license/verify` | `GET` (form/`-d` body) | Returns license object (or 4xx if invalid) |
| `/license/disable` | `PUT` | Disable a key (also auto-disabled on refund) |
| `/license/enable` | `PUT` | Re-enable a key |
| `/license/usage` | `PUT` | Increment usage counter |
| `/license/decrease` | `PUT` | Decrement usage counter |

License object shape:
```json
{
  "enabled": true,
  "product_link": "mVT0",
  "license_key": "WTKP4-66NL5-HMKQW-GFSCZ",
  "buyer_email": "contact@payhip.com",
  "uses": 0,
  "product_name": "Example Product",
  "date": "2024-02-22T11:23:05+00:00"
}
```

Notable differences vs Gumroad:
- No explicit `refunded` flag — Payhip auto-flips `enabled` to `false` when a refund happens.
- No subscription/recurring fields.
- License key alone is **not enough** to identify the product (no product ID returned). Therefore for Payhip the backend must try each known product secret until one succeeds. Plan: store an internal `Product` row per Payhip product with its `product_link` ("mVT0"-style key) + `product_secret`.

---

## 5. Backend changes (`osci-render-api`)

### 5.1 Schema additions

```python
# models.py — additions
class Product(Base):
    # existing
    gumroad_product_id  = Column(String, nullable=True)
    gumroad_permalink   = Column(String, nullable=True)
    # NEW
    provider              = Column(Enum("gumroad", "payhip"), nullable=False)
    payhip_product_link   = Column(String, nullable=True)
    payhip_product_secret = Column(String, nullable=True)  # stored server-side; never returned by admin serializers
    # existing fields stay

class Version(Base):
    """A published product release."""
    id            = Column(Integer, primary_key=True)
    product_id    = Column(Integer, ForeignKey("product.id"), nullable=False)
    semver        = Column(String, nullable=False)         # "2.8.10.8"
    release_track = Column(Enum("alpha", "beta", "stable"), default="stable")
    notes_md      = Column(Text)
    min_supported_from = Column(String, nullable=True)     # earliest version that can self-update
    released_at   = Column(DateTime, default=now)
    is_yanked     = Column(Boolean, default=False)

class VersionAsset(Base):
    """A downloadable artifact for one Version."""
    id            = Column(Integer, primary_key=True)
    version_id    = Column(Integer, ForeignKey("version.id"), nullable=False)
    variant       = Column(Enum("free", "premium"), nullable=False)
    platform      = Column(Enum("mac-arm64", "mac-x86_64", "mac-universal",
                                "win-x86_64", "linux-x86_64"))
    artifact_kind = Column(Enum("pkg", "exe", "appimage", "tar.gz", "zip"))
    object_key    = Column(String, nullable=False)         # R2 key
    sha256        = Column(String, nullable=False)
    ed25519_sig   = Column(String, nullable=False)         # signature over (semver|platform|sha256)
    uploaded_at   = Column(DateTime, default=now)
    is_yanked     = Column(Boolean, default=False)
```

Implementation note: the original draft put platform/artifact fields directly
on `Version`. The code now splits those into `VersionAsset` so one product
release can have many platform + free/premium asset rows without duplicating
release-level notes and track metadata.

The `ed25519_sig` exists so the **client** can verify the manifest hasn't been tampered with even if HTTPS were stripped/MITM'd. The corresponding **public key is hard-coded** in the JUCE module. The private key lives only on the build/release machine — the backend itself does not need it (it just stores the signature alongside the artifact).

### 5.2 Endpoints

#### `POST /api/license/activate`
Replaces today's `/api/verify-license` for the new flow.
```json
// Request
{ "license_key": "...", "product_hint": "osci-render"|"sosci"|null }

// Response (200)
{
  "license_key": "...",
  "product": { "id": 1, "slug": "osci-render", "name": "osci-render premium" },
  "buyer_email": "...",
  "provider": "gumroad"|"payhip",
  "valid": true,
  "issued_at": "2026-01-01T00:00:00Z",
  "token": "<base64 ed25519-signed JWT-ish blob>"
}
```

The `token` payload (signed with backend's private key, NOT the same key used to sign artifacts):
```json
{ "kid": 1, "lk": "WTKP4-...", "pid": "osci-render-premium", "iat": 169..., "exp": 169... }
```
- `exp` ~ 30 days. Renewed on every successful re-check.
- Plugin treats a valid unexpired token as "premium ON" with no network call needed.
- After `exp`, plugin attempts re-validation; if offline, premium stays on for a configurable grace (e.g. another 14 days), then degrades.

#### `GET /api/version/latest?product=osci-render&release_track=stable&variant=premium&platform=mac-arm64&current=2.4.0.0`
Returns:
```json
{
  "latest": "2.5.0.0",
  "is_newer": true,
  "min_supported_from": "2.0.0.0",
  "manifest": {
    "semver": "2.5.0.0",
    "platform": "mac-arm64",
    "artifact_kind": "pkg",
    "size_bytes": 218445312,
    "sha256": "...",
    "ed25519_sig": "..."
  },
  "notes_md": "..."
}
```
No license required for `stable` or `beta` metadata. `alpha` metadata requires
an authenticated admin session so internal CI/test builds do not appear to
normal users.

#### `POST /api/version/download-url`
```json
// Request
{ "product": "osci-render", "semver": "2.5.0.0", "release_track": "stable", "variant": "premium", "platform": "mac-arm64", "license_token": "<signed activation token>" }

// Response (200)
{ "url": "https://r2-presigned.../osci-render-2.5.0.0-mac-arm64.pkg?X-Amz-...", "expires_at": "..." }
```
- `alpha` download URLs require an authenticated admin session. `beta` and `stable` free downloads are public.
- `premium` variant download URLs require a signed premium activation token for the requested product.
- Current implementation publishes separate `free` and `premium` variant assets; feature availability remains compile-time, while licensing/update flows choose which artifact may be downloaded.
- Download analytics keyed by license are still a TODO; generic HTTP audit logging now exists for mutating routes.

### 5.3 Cloudflare R2 setup
- One bucket: `osci-releases`.
- Layout: `{product}/{version}/{variant}/{platform}/{filename}` e.g. `osci-render/2.5.0.0/premium/mac-arm64/osci-render-2.5.0.0-mac-arm64.pkg`.
- `release_track` is deliberately absent from the object key because alpha/beta/stable promotion is a mutable database property.
- Backend uses the AWS SDK (`boto3`, S3-compatible) with R2 credentials from env to issue presigned `GET` URLs (10-minute TTL).
- Public-read disabled. All access via presigned URLs.

### 5.4 Release publishing
A small `ci/publish_release.py` takes built artifacts + a private Ed25519 key and:
1. Computes SHA-256.
2. Signs `(semver || platform || sha256)` with Ed25519.
3. POSTs metadata to authenticated `/api/admin/version/publish`.
4. Receives a presigned R2 upload URL and object key from the backend.
5. Uploads the artifact to R2.

The osci-render build workflow now always publishes with `--release-track alpha`.

The private signing key lives **only** on the release machine (and a backup in 1Password). Never in the backend.

---

## 6. Client (JUCE) architecture

### 6.1 New module — `modules/osci_licensing/`

Standard JUCE module layout. Depends on `juce_core`, `juce_events`. Optionally pulls in libsodium for Ed25519 (or vendor `tweetnacl`/JUCE's built-in if a small sub-set suffices).

```
modules/osci_licensing/
├── osci_licensing.h          # module header
├── osci_licensing.cpp        # umbrella .cpp that #includes the rest
├── license/
│   ├── osci_LicenseManager.h/.cpp    # Status enum, activate/deactivate, token cache, getStateForUi
│   └── osci_LicenseToken.h/.cpp      # Ed25519 token parse + verify (Monocypher); embedded public key
├── network/
│   └── osci_BackendClient.h/.cpp     # thin HTTP wrapper; logs each request/response to JUCE logger
├── update/
│   ├── osci_UpdateChecker.h/.cpp     # /api/version/latest polling
│   ├── osci_Downloader.h/.cpp        # verified artifact download with Progress callback
│   └── osci_InstallerLauncher.h/.cpp # platform handoff (open -W pkg / ShellExecute / file copy)
└── hardware/
    └── osci_HardwareInfo.h/.cpp      # platform slug ("mac-arm64" etc), storage dir
```

> **Actual state (2026-05-03):** The token verification is embedded inside `LicenseManager` rather than a separate `osci_LicenseToken` file; `osci_HardwareInfo` provides `getDefaultStorageDirectory(product)` and `getPlatform()`. The Ed25519 verifier uses vendored Monocypher.

### 6.2 Public API sketch

```cpp
namespace osci {

struct License {
    juce::String key;
    juce::String productSlug;     // "osci-render" | "sosci"
    juce::String email;
    juce::Time   issuedAt;
    juce::Time   expiresAt;
};

class LicenseManager {
public:
    enum class Status { Free, PremiumValid, PremiumCachedToken, ExpiredOffline };
    // PremiumValid   — token is valid and not within offline grace
    // PremiumCachedToken — token valid but within offline grace period (no recent server contact)
    // Serialized as: "free" / "premium_valid" / "premium_cached_token" / "expired_offline"

    Status status() const noexcept;
    bool   hasPremium (const juce::String& featureGroup = {}) const;

    // Returns Result with localized error message on failure.
    juce::Result activate (juce::StringRef key);    // hits backend, persists token
    void         deactivate();                       // wipes token + decrements usage server-side

    // Called periodically (e.g. on launch + every 30d).
    void scheduleBackgroundRefresh();

    juce::ValueTree getStateForUi() const;           // for binding to SettingsComponent
};

struct VersionInfo {
    juce::String semver;
    juce::String platform;          // "mac-arm64" etc
    juce::String artifactKind;      // "pkg" / "exe" / "appimage"
    juce::int64  sizeBytes;
    juce::String sha256;
    juce::String ed25519Signature;
    juce::String notesMarkdown;
};

class UpdateChecker : private juce::Thread {
public:
    enum class Channel { Stable, Beta };
    std::optional<VersionInfo> checkForUpdate (juce::StringRef product,
                                               juce::StringRef currentVersion,
                                               Channel = Channel::Stable);
};

class Downloader : private juce::Thread {
public:
    using Progress = std::function<void(double fraction, juce::int64 downloadedBytes)>;
    juce::Result downloadAndVerify (const VersionInfo&,
                                    juce::StringRef licenseKey,    // may be empty
                                    Progress);
    juce::File getDownloadedFile() const;
};

class InstallerLauncher {
public:
    /** Launches the platform-appropriate install path.
     *  - macOS: `open <pkg>` (lets the system installer prompt for admin).
     *  - Windows: ShellExecute "runas" on the .exe with /SILENT /SUPPRESSMSGBOXES.
     *  - Linux: extracts the AppImage / copies VST3 to ~/.vst3/.
     *  Caller is expected to have already saved its state and shown a "quitting" dialog.
     */
    bool launchAndExitHost (const juce::File& installerOrPayload);
};

} // namespace
```

#### Installer completion detection — reviewed recommendation

> **Status:** NOT implemented. Do **not** add generic plugin-side
> "completion detection" based on child-process exit. Treat installer launch as
> a handoff, then verify the result on the next app/plugin startup.

The earlier `launchAndMonitor(...)` idea is too easy to make misleading. From
inside a plugin editor we cannot reliably prove that an elevated OS installer
has finished writing files, that a DAW has fully released the old plugin binary,
or that the editor object watching the process will still exist when the
installer exits.

Recommended contract:

1. **Before handoff**, the plugin downloads and verifies the artifact, performs
   best-effort DAW-running detection, then writes a pending-install marker under
   the product storage directory. The marker should include product, old
   version, target version, platform, variant, artifact path, artifact SHA-256,
   and created-at time.
2. **Handoff**, not monitor: `InstallerLauncher` opens the installer/payload and
   returns only whether launch was successfully requested. UI copy should say
   "Installer launched. Close your DAW and reopen it after installation
   finishes." It should not say "Installation complete."
3. **Actual completion proof happens later**. On next app/plugin startup, read
   the pending marker and verify the installed product/version by probing the
   installed app/plugin metadata or by reading a result marker written by the
   dedicated installer app. If the expected version is present, clear the marker
   and optionally show a one-time success message. If not, expire the marker or
   offer to retry.
4. **The future `osci-installer` app may monitor its own work**, because it owns
   the install UI and lifetime. Even there, success should mean "the expected
   files/version are present after install", not "the child process exited".

Why process-exit monitoring is not a good plugin-level signal:

| Platform | Problem |
|---|---|
| macOS `.pkg` | `open -W <pkg>` waits for `Installer.app`/document handling, not a durable proof that all package payloads and scripts are complete. The plugin also should not scrape `/var/log/install.log`; it is localized/format-unstable and may require permissions. |
| Windows `.exe` | UAC elevation and installer bootstrappers can detach from the launching process. `ShellExecuteEx(..., SEE_MASK_NOCLOSEPROCESS)` may give a handle to a stub that exits before the real elevated installer finishes. Inno/NSIS options vary by package settings. |
| Linux payloads | Direct copy can be made deterministic, but then it belongs in installer/update code that owns the copy operation. AppImage launch exit means the app exited, not that plugins were installed. |
| DAW-hosted plugin | The host may keep the old module loaded even after files are replaced. The editor can be destroyed at any time, so callbacks must not own user-facing truth. |

This means `InstallerLauncher` should stay deliberately small for now:
`launchAndExitHost(...)` may become platform-specific for launch mechanics and
DAW warnings, but it should not promise completion. A separate
`InstallState`/marker helper is the right abstraction when we implement this.

### 6.3 Token verification — embed the public key

```cpp
// osci_LicenseToken.cpp
static constexpr unsigned char kBackendPublicKey[32] = {
    /* hex bytes of the backend's signing public key */
};
```

The token is a small base64-encoded blob: `{ payload-bytes || ed25519-signature }`. Verification is offline-only: deserialize, check `exp` against `juce::Time::getCurrentTime()`, verify signature with `kBackendPublicKey`. **No network needed at startup.**

For artifact integrity:
```cpp
static constexpr unsigned char kReleaseSigningPublicKey[32] = { /* different key */ };
```
Used to verify the `ed25519_sig` returned by `/api/version/latest`.

Two separate keys means leaking the backend key (e.g. via a server compromise) does not enable forging release artifacts.

### 6.4 Local file layout

```
~/Library/Application Support/osci-render/      (macOS)
%APPDATA%/osci-render/                           (Windows)
~/.config/osci-render/                           (Linux)
├── license.dat       # opaque base64 token (Ed25519-signed by backend)
├── update-cache.json # last successful update check (avoid hammering API)
└── downloads/        # in-progress download chunks
```

`license.dat` is **not encrypted client-side**. Encrypting it adds zero security (the key would have to live in the binary) and just frustrates legitimate users who want to copy their license to a new machine.

### 6.5 Plugin self-update flow

VST3/AU loaded inside a DAW:
1. After editor launch, schedule an async `UpdateChecker::checkForUpdate(...)` for `stable`, or `beta` only if the hidden beta setting has been enabled.
2. If newer, surface a top-right non-modal prompt in the editor.
3. User clicks Update -> modal dialog asks them to save work / close the DAW host if necessary. On confirm, plugin asks the backend for a download URL with the signed activation token, `Downloader::downloadAndVerify`, then `InstallerLauncher::launchAndExitHost(...)`.
4. User clicks Later -> hide that same version for 48 hours.
5. The launcher writes a small marker file the new install will see and shows "Installer is running. Please reopen your DAW after it finishes."

Beta is intentionally tucked away: clicking the About-version line five times toggles beta updates on/off, temporary countdown messages clear after roughly two seconds, and alpha never appears in plugin UI. When beta mode is enabled, a top-right `Beta updates` button acts as the visible indicator and opens the account/update panel, where `Use stable updates` disables beta mode.

Premium builds are intentionally blocking: if a premium binary starts without a valid premium activation token, the account overlay is shown without backdrop/close dismissal until activation succeeds. For osci-render only, the escape hatch is explicit and artifact-based: `or install free version` downloads/verifies the latest stable free build and hands it to the installer, rather than pretending a premium binary can run as a free binary at runtime. sosci is premium-only, so it does not show a free fallback. Free binaries still run normally and can use a premium token to download the separate premium artifact.

We deliberately do **not** try to swap the `.vst3` while the host has it loaded — both Win (file lock) and macOS (cached dylib in process memory) make this fragile.

### 6.6 Keeping the compile-time `OSCI_PREMIUM` flag

`OSCI_PREMIUM` remains the build-time switch for premium code paths. The client licensing code must not unlock premium features inside a free binary at runtime.

The runtime license state is used for activation, cached entitlement display, and choosing which separate artifact an installer/update flow may download. Feature availability remains determined by the compiled product variant.

---

## 7. New Projucer project — `osci-installer.jucer`

Tiny JUCE GUI app, ~5–10 MB shipped binary. Same JUCE-based codebase as the plugin so we get free Linux+Win+Mac portability without bringing in Qt, Electron, or .NET.

### 7.1 UI flow

```
  ┌────────────────────────┐
  │   Welcome screen        │
  │   [Continue free]       │
  │   [I have a license]    │
  └──────────┬──────────────┘
             ▼
  ┌────────────────────────┐
  │   Paste license key     │
  │   [Verify]              │   ← LicenseManager::activate()
  └──────────┬──────────────┘
             ▼
  ┌────────────────────────────────────┐
  │   Choose what to install            │
  │   [x] osci-render (premium licensed)│
  │   [x] sosci (free)                  │
  │   [Install]                         │
  └──────────┬──────────────────────────┘
             ▼
  ┌────────────────────────┐
  │  Downloading...         │   ← Downloader, progress bar
  │  [Cancel]               │
  └──────────┬──────────────┘
             ▼
  ┌────────────────────────┐
  │  Running installer...   │   ← InstallerLauncher
  │  [Done]                 │
  └────────────────────────┘
```

### 7.2 Build setup
- Add `osci-installer.jucer` alongside `osci-render.jucer` and `sosci.jucer`.
- Module path includes `modules/osci_licensing/`.
- Standalone-only (no plugin formats).
- Code-signed and notarized like the main app on macOS; signed on Windows with the existing certificate.
- Initially version `1.0.0`. We aim never to bump it.

### 7.3 Self-update of the installer itself
The installer also calls `UpdateChecker` for `product=osci-installer`. If a new installer version is required (rare), it offers to upgrade itself first, then reruns. This means we can fix bugs in the installer without forcing a manual download from the website.

---

## 8. Platform-specific install paths

### 8.1 macOS
- Artifact: `.pkg` (built today by Packages from `.pkgproj`).
- Install handoff: `InstallerLauncher` opens the `.pkg` with the system Installer app. Do not use `open -W` as a completion signal; it may be useful only as a launch/wizard lifetime hint in a dedicated installer app.
- VST3 → `/Library/Audio/Plug-Ins/VST3/`, AU → `/Library/Audio/Plug-Ins/Components/`, Standalone → `/Applications/`.
- Code-signing: existing Developer ID + notarization pipeline (`ci/setup-env.sh`, `ci/build.sh`) carries over unchanged.

### 8.2 Windows
- Artifact: `.exe` from Inno Setup (`packaging/*.iss`).
- Install handoff: `ShellExecuteEx` with `lpVerb = "runas"` and conservative parameters such as `/NORESTART`. Silent flags are acceptable only after testing with the actual Inno script, because silent close-app behavior can be hostile from inside a DAW. Do not treat the returned process handle as install completion proof.
- VST3 → `C:\Program Files\Common Files\VST3\`.
- Code-signing: existing Azure Trusted Signing pipeline.

### 8.3 Linux
- Artifact: a `.tar.gz` containing `osci-render.AppImage`, `osci-render.vst3/`, `osci-render.lv2/` (when we add LV2).
- Install: direct file copy from updater/installer code to `~/.vst3/`, `~/.lv2/`, and `$XDG_DATA_HOME/applications/` (no root needed). Because this is our own copy operation, completion can be based on copied files + hash/version verification rather than external process exit.
- Existing AppImage tooling stays.

### 8.4 DAW-running detection
Before launching the installer, warn if a known DAW process is running.
Implementation should be best-effort and non-destructive:

- Enumerate processes (`NSWorkspace`/`ps` on macOS, `/proc`/`ps` on Linux, `EnumProcesses` + image names on Windows).
- Match normalized executable names and, on macOS, bundle identifiers where available. Include Logic, Ableton Live, REAPER, Bitwig Studio, Cubase/Nuendo, Studio One, FL Studio, Pro Tools, Reason, Waveform, GarageBand, MainStage, and common plugin hosts used for testing.
- If any match is found, show a warning and require explicit confirmation before handoff. Do not attempt to close the host from plugin code.
- Treat missed detections as possible. The install result is still verified later by the pending marker/version probe.

---

## 9. Security considerations

| Threat | Mitigation |
|--------|------------|
| Stolen license key | Backend rate-limits activations per key; `uses` count visible to admin; admin can disable via Gumroad/Payhip → propagated on next refresh. |
| Forged license token | Ed25519 verification client-side with embedded public key. |
| Forged update manifest (MITM) | Ed25519-signed `manifest` field; client verifies before downloading. |
| Tampered downloaded artifact | SHA-256 in manifest; client recomputes after download. |
| Backend compromise | Backend can mint license tokens but **cannot** sign release artifacts — separate signing key on the build machine. |
| R2 credentials leak | Presigned URLs are short-lived (10 min). Bucket is private. |
| Telemetry abuse / privacy | Client API traffic logging is disabled by default and redacts known secrets when explicitly enabled for local debugging. Backend audit/download analytics should avoid raw license keys and store only the minimum identifiers needed for support/abuse handling. |
| Public download endpoint scraping | Free downloads are unauthenticated by design — that's fine, the binary doesn't unlock premium without a token. |

OWASP-relevant items to keep front-of-mind in the Flask code:
- Use parameterized SQLAlchemy queries (already the case).
- All admin endpoints require Google OAuth (already the case).
- Add CSRF protection on the new admin POST endpoints.
- Rate-limit `/api/license/activate` (e.g. Flask-Limiter, 10/minute/IP).
- Log all activation attempts (success + failure) for abuse detection.

---

## 10. Open questions — RESOLVED

All items previously open in this section have been answered (see Decisions table in §3, rows 9–15).

---

## 13. Hosting & infrastructure

### 13.1 Topology

```
     api.osci-render.com  ── Cloudflare DNS ──▶  Fly.io (Flask + gunicorn)
                                                       │
                                                       ├── DATABASE_URL ──▶ Neon Postgres (free tier, 3GB)
                                                       ├── R2 creds  ─────▶ Cloudflare R2 (osci-releases bucket)
                                                       └── Secrets:
                                                              GUMROAD_ACCESS_TOKEN
                                                              PAYHIP_PRODUCT_SECRET_*  (per product)
                                                              ED25519_TOKEN_PRIVATE_KEY
                                                              R2_ACCESS_KEY_ID / R2_SECRET_ACCESS_KEY
                                                              GOOGLE_OAUTH_CLIENT_ID/SECRET
                                                              SECRET_KEY (Flask)
```

### 13.2 Why Fly.io + Neon + R2

- **Fly.io** — `flyctl deploy` pushes from the existing `osci-render-api` repo with no code changes; auto-builds the Procfile via Buildpacks or a 5-line Dockerfile. Shared-cpu-1x@256MB instance is ~$1.94/mo, free for first machine within the included allowance. Region: `lhr` (London) or wherever james is.
- **Neon** — serverless Postgres, free tier 3GB storage / 10GB-month transfer. Branchable (great for staging). `psql $DATABASE_URL` works out of the box. Provisioned via `neonctl` CLI.
- **Cloudflare R2** — bucket + access keys created via `wrangler r2 bucket create`. Bucket private; presigned URLs issued by backend (10-min TTL). DNS already on Cloudflare so no separate setup.
- All three together: **~$0–2/mo at expected hobby volumes** (well within free tiers).

### 13.3 CLI tools needed end-to-end

| Tool | Purpose | Install (macOS) |
|------|---------|-----------------|
| `flyctl` | Fly.io app deploy + secrets + logs | `brew install flyctl` |
| `neonctl` | Neon project + branch + role mgmt | `npm i -g neonctl` (or `brew install neonctl`) |
| `wrangler` | Cloudflare R2 bucket + DNS records | `npm i -g wrangler` |
| `psql` | Migrations / sanity checks | `brew install libpq` |
| Existing `heroku` (only for migration cutover) | Pull data + secrets from old deploy | `brew install heroku/brew/heroku` (already installed) |

All non-interactive after initial `flyctl auth login`, `neonctl auth`, `wrangler login` (one-time browser-based logins).

### 13.4 Migration plan from Heroku

1. `flyctl launch` from `osci-render-api/` — creates `fly.toml` and Dockerfile.
2. `neonctl projects create --name osci-render-api` — get connection string.
3. `flyctl secrets set DATABASE_URL=...` plus all OAuth/Gumroad/Payhip/Ed25519/R2 secrets (pull current values from `heroku config -a osci-render-api`).
4. `pg_dump` from Heroku Postgres → `psql` into Neon.
5. `flyctl deploy` — releases run `flask db upgrade` automatically.
6. Update Cloudflare DNS for `api.osci-render.com` → Fly's anycast IPs (just swap the A/AAAA records).
7. Verify, then `heroku ps:scale web=0` and shut down the Heroku app.

### 13.5 R2 setup

1. `wrangler r2 bucket create osci-releases`.
2. Cloudflare dashboard → R2 → **Manage R2 API Tokens** → create scoped token with read/write to `osci-releases` only.
3. Add `R2_ACCOUNT_ID`, `R2_ACCESS_KEY_ID`, `R2_SECRET_ACCESS_KEY` to Fly secrets.
4. (Optional) Public custom subdomain `releases.osci-render.com` for unauthenticated/free downloads — bucket stays private but a CF Worker can mint signed redirects. Skip in v1.

### 13.6 Estimated monthly cost

| Component | Hobby (assumed: <1000 downloads/mo, <100MB DB) | If volume grows 10× |
|-----------|------------------------------------------------|---------------------|
| Fly app (1× shared-cpu-1x@256) | ~$0 (within free allowance) | ~$2 |
| Neon Postgres | $0 (free tier) | $0 (still under 3GB) |
| R2 storage (~5 GB of artifacts) | ~$0.08/mo | ~$0.30/mo |
| R2 egress | $0 (free) | $0 |
| Cloudflare DNS | $0 | $0 |
| **Total** | **~$0** | **~$2–3** |

---

## 11. Implementation order and current checklist

0. **Migrate hosting first** (independent prerequisite) — see §13.
  - [x] Provision Fly app + Postgres-backed deployment path.
  - [x] Deploy Flask app to Fly and use `api.osci-render.com` for production API traffic.
  - [x] Verify existing Gumroad flows through the current API.
1. **Backend feature work** — extend `osci-render-api`:
  - [x] Add Payhip columns to `Product`; seed/local/admin support included.
  - [x] Implement Payhip license verification and refactor license activation to try Gumroad/Payhip.
  - [x] Add `Version` + `VersionAsset` models and migrations.
  - [x] Add `/api/version/latest` + `/api/version/download-url` with release-track support.
  - [x] Add Ed25519 token signing for `/api/license/activate`.
  - [x] Wire R2 presigned upload/download URLs.
  - [x] Add admin UI rows for managing versions, version assets, products, and Payhip metadata.
  - [x] Add automated audit logging for mutating HTTP routes.
2. **Generate signing keys** (release-artifact key + token-signing key). Store privately.
  - [x] Backend expects `ED25519_TOKEN_PRIVATE_KEY`, `RELEASE_SIGNING_PUBLIC_KEY`, and CI release-signing private key secrets.
  - [x] Production backend/release public keys are embedded in `osci-render.jucer`, `sosci.jucer`, and `osci-render-test.jucer`.
  - [ ] Confirm the private-key checklist outside this design doc: Fly `ED25519_TOKEN_PRIVATE_KEY`, Fly `RELEASE_SIGNING_PUBLIC_KEY`, GitHub/CI release-signing private key, GitHub/CI `PUBLISH_API_TOKEN`, 1Password backup item names, and rotation steps including token `kid` handling.
3. **CI publishing pipeline**.
  - [x] `ci/publish_release.py` signs artifacts, registers metadata, uploads to R2.
  - [x] GitHub Actions always publishes osci-render builds as `alpha`.
  - [x] 2.8.10.8 alpha publish path succeeded after one pluginval rerun.
  - [x] Promotion workflow is admin-driven: CI publishes `alpha`, then operator promotes to `beta`/`stable` in admin UI.
4. **JUCE module** `modules/osci_licensing/` — implement step-by-step with unit tests.
  - [x] Exists as public GPLv3 submodule at `modules/osci_licensing`.
  - [x] Provides `LicenseToken`, `LicenseManager`, `BackendClient`, `UpdateChecker`, `Downloader`, `InstallerLauncher`, and `HardwareInfo`.
  - [x] Includes a production Ed25519 verifier via vendored Monocypher.
  - [x] Licensing unit tests cover token expiry/grace, release-signing contract, real Ed25519 verification, cached free-token state, and hardware helpers.
  - [x] Real backend/release public key values are embedded in app build definitions.
5. **Wire into osci-render** — license/update UI while keeping compile-time premium variants.
  - [x] Shared `CommonAudioProcessor` owns a per-product `LicenseManager` and loads cached tokens on startup.
  - [x] osci-render and sosci expose a shared License and Updates UI from the About menu.
  - [x] Manual activation, deactivation, release-track selection, update check, verified download, and installer handoff are wired.
  - [x] Premium downloads now send the cached signed activation token; backend blocks premium presigned URLs without a valid premium token.
  - [x] A free build with a valid premium token can target the separate premium artifact.
  - [x] About version text toggles beta/stable updates after 5 clicks; alpha is hidden from plugin UI.
  - [x] A top-right `Beta updates` indicator button appears while beta mode is active and opens the account/update panel.
  - [x] The account/update panel no longer exposes a release-track dropdown; it offers `Use stable updates` only when beta mode is active.
  - [x] Premium builds without premium activation show a required, non-dismissible account overlay.
  - [x] Update action is combined into one download/verify/install flow instead of separate Download and Install buttons.
  - [x] Feature gating remains compile-time; `OSCI_PREMIUM` continues to gate premium code paths.
6. **`osci-installer.jucer`** — minimal welcome → key → install flow.
  - [ ] Not started.
7. **Update flow integration in plugin editor** — banner UI + handoff to installer-style code path.
  - [x] Manual update check/download/install flow exists in the License and Updates overlay.
  - [x] Editor launch schedules an update check and shows a top-right prompt when a newer version exists.
  - [x] Later dismisses the same version for 48 hours.
  - [ ] Update UX still needs polish and deeper installer/DAW-specific behavior.

Each remaining client-facing item is independently buildable and testable.

---

## 14. References

- Gumroad licenses API — <https://gumroad.com/api#licenses>
- Payhip licenses API — `./API Reference - Payhip.html` (local copy; the live page sits behind Cloudflare Access)
- Existing backend — `./osci-render-api/app.py`, `./osci-render-api/models.py`
- Sparkle (macOS) — <https://sparkle-project.org>, MIT
- WinSparkle (Windows) — <https://winsparkle.org>, MIT
- Pamplejuce (packaging reference) — <https://github.com/sudara/pamplejuce>
- Sudara's pkgbuild/productbuild tutorial — <https://forum.juce.com/t/pkgbuild-and-productbuild-a-tutorial-pamplejuce-example>
- Ed25519 / libsodium — <https://libsodium.gitbook.io/doc/public-key_cryptography/public-key_signatures>
- GitHub REST: release assets — <https://docs.github.com/en/rest/releases/assets> (researched but not chosen)
- Cloudflare R2 presigned URLs — <https://developers.cloudflare.com/r2/api/s3/presigned-urls/>
