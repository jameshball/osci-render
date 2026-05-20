# Proprietary-Compatible Builds

This document records the project policy for code paths that are intended to be reusable from proprietary software.

This is an engineering policy document, not legal advice.

## Public GPL Release And Private Use

The public osci-render repositories and modules may be distributed under GPLv3. That public GPL grant remains available to public recipients of the GPL version.

James H Ball is treated here as the copyright owner of the reusable code intended for private commercial use. As copyright owner, he can use his own copyrightable code under private commercial terms without publishing or displaying a separate secondary public license for that private use. Existing public GPL recipients keep the GPL rights they already received.

`OSCI_PROPRIETARY_BUILD` is an engineering build boundary, not a license grant. Private commercial distribution still depends on copyright ownership, any private license records, third-party notices, and separate commercial licenses where required.

## Compatibility Boundary

Code is proprietary-compatible when it can be used in a private commercial product without pulling that product under GPL or another incompatible copyleft obligation.

The proprietary-compatible surface may include:

- code owned by James H Ball
- code with a permissive license compatible with proprietary use
- dependencies covered by a separate commercial license
- generated or bundled assets whose provenance and commercial-use rights are clear

The proprietary-compatible surface must exclude, replace, or compile out:

- GPL-only or copyleft dependencies that would impose source-distribution obligations on the proprietary product
- assets without clear commercial-use rights
- copied snippets with unclear provenance
- third-party contributions that were not accepted under terms compatible with private commercial use

## `OSCI_PROPRIETARY_BUILD`

Set `OSCI_PROPRIETARY_BUILD=1` when compiling a proprietary consumer or when verifying that reusable modules avoid incompatible dependencies.

In this mode, code should compile without GPL-only or unresolved-provenance dependencies. It may still use permissively licensed code and separately licensed framework/platform dependencies.
Project-owned assets may remain available in this mode; only assets with unclear or incompatible rights should be excluded, replaced, or separately licensed.

When `OSCI_PROPRIETARY_BUILD` is unset or `0`, GPL osci-render builds may continue to use GPL-compatible dependencies and GPL-distributed application assets.

The mode name is intentionally about proprietary compatibility rather than exclusive ownership. Permissively licensed code can remain in this path.

For proprietary distribution, JUCE and any other framework or platform dependencies must be covered by suitable non-GPL or commercial terms.

## Optional Feature Flags

Optional dependencies should be controlled by explicit feature flags rather than by implicit module assumptions.

- `OSCI_RENDER_CORE_ENABLE_CHOWDSP_RESAMPLING=1` enables core sample-rate conversion backed by ChowDSP. Projects enabling it must include the required ChowDSP modules.
- `OSCI_GUI_ENABLE_VISUALISER=1` enables the visualiser renderer in `osci_gui`. Projects enabling it must include the renderer's required modules, including `osci_render_core` and JUCE OpenGL support.
- `OSCI_GUI_ENABLE_CHOWDSP_RESAMPLING=1` enables visualiser upsampling backed by ChowDSP. Projects enabling it must include the required ChowDSP modules.

These feature flags must remain off in `OSCI_PROPRIETARY_BUILD` builds unless the dependency has been separately cleared for proprietary use and the guard is intentionally updated.

## Contribution Policy

Do not accept third-party contributions into proprietary-compatible reusable paths unless the contribution is under terms compatible with private commercial use. Otherwise, keep the contribution outside `OSCI_PROPRIETARY_BUILD` builds or reimplement it independently.

When moving code into a proprietary-compatible module, record any non-obvious license or provenance assumptions close to the module docs.

## Verification

Each reusable module that participates in proprietary-compatible builds should have at least one build target or smoke test that defines `OSCI_PROPRIETARY_BUILD=1`.

Normal GPL app builds should continue to build with `OSCI_PROPRIETARY_BUILD` unset.

## References

- U.S. Copyright Office, copyright ownership overview: https://www.copyright.gov/what-is-copyright/
- GNU GPL FAQ, developer not bound by their own GPL grant to others: https://www.gnu.org/licenses/gpl-faq.en.html#DeveloperViolate
- GNU GPL FAQ, existing public GPL rights cannot be withdrawn: https://www.gnu.org/licenses/gpl-faq.en.html#CanDeveloperThirdParty
