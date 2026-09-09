# Multi-object scenes for osci-render

**Extend the existing workflow with an optional scene editor. Keep the main interface and its effects panel familiar.** A scene contains one or more objects and its own camera. The project retains one shared effects chain, applied to whichever scene is playing.

This document defines the agreed first version. The broader [feature research](feature-research.md) remains an opportunity list; its snapshots, macros, per-object effects and other expansions are not dependencies of this release.

## First-version decisions

| Area | Agreed behaviour |
|---|---|
| Normal workflow | Opening and selecting files follows the current flow; each existing file conceptually becomes a one-object scene. Simple users need not enter the scene editor. |
| Scene selection | Existing arrows, File Select automation and MIDI Program Change select scenes. |
| Editor | A right-hand Scene panel alongside Effects and Examples. The live visualiser remains visible on the left. |
| Interaction | Select and arrange objects directly in an interactive 3D preview, with movement, rotation and scale handles. Numeric-only placement is insufficient. |
| Camera | One camera per scene. Freely moving the editor camera changes the actual output camera. Each scene remembers its camera. |
| Effects | One project-wide effects chain, as today. No per-object effect chains or per-scene effect recall in version one. |
| Object count | No fixed product limit imposed by automation capacity. Actual capacity depends on available resources and rendering cost. |
| DAW exposure | Explicitly expose an object or camera. Each consumes one slot from a fixed project-wide pool and exposes a set of controls. |
| Exposure identity | Slots stay attached to their chosen object/camera across scene changes. They do not follow selection or get reused automatically by another scene. |
| Source support | All current source types work in version one, including Lua, animations, audio and live inputs such as Blender/texture input. |
| Geometry | Reuse the existing 3D point representation. No new distinction is required merely to place a nominally 2D source in 3D. |
| Playback | One playback clock for the active scene; existing global speed and scrub controls affect its animated objects together. |
| Animation lengths | Each animation loops at its own duration when looping is enabled. Different lengths are not stretched to match. |
| Scene switching | Preserve current file-switching playback behaviour. Do not introduce a new restart/resume preference. |

## What using it looks like

A user opens a cube and adds effects as they do today. The cube is the sole object in the selected scene. Opening another file through the normal workflow continues to follow current file-opening behaviour, producing another selectable one-object scene rather than unexpectedly inserting an object into the existing composition.

To add text alongside the cube, the user selects Scene, then uses the existing plus button or Examples tab. The shared browser is labelled Add to scene; imported files and examples become objects in the current scene. They select the text in the preview, position it beneath the cube with handles, and move the camera to frame both. Effects returns to the shared effects panel and normal file-opening behaviour.

Adding Wobble in the existing effects panel affects the composed result. The user cannot give Wobble only to the cube in version one. Switching to another scene retains that same effects chain and its current settings, while using the newly selected scene’s own camera and objects.

The distinction between normal file opening and importing inside the scene editor must be apparent from context. A scene does not need to be empty before opening its editor: editing the already loaded single object is the normal entry point.

## The existing effects panel stays

Do not make the effects panel follow the selected object. Do not add an object/scene chain selector or several side-by-side effect panels in version one. Selection in the editor changes what is being arranged; it does not change which effects are being edited in the main interface.

The conceptual processing order is:

**Source objects → individual transforms → scene composition and camera → existing shared effects behaviour.**

This is a product model, not permission to move all effects after the final audio mix. The current engine applies many effects per synth voice before mixing. Preserve those semantics while adding composition, and establish the exact placement of camera/projection relative to existing Perspective and other controls during implementation. A one-object scene should retain the current result under equivalent settings. [L1][L2]

Per-object effects remain a future capability. Keep stable object identities and an object-local processing boundary so that transforms need not become inseparable from a flattened scene signal. Do not build empty per-object racks, reserve a vast parameter inventory, or implement effect cloning solely for hypothetical future use.

## Integrated scene panel

The right-hand panel has Effects, Scene, Editor and Examples tabs. The existing visualiser and quick controls remain visible on the left. There is no scene overlay or separate scene import dialog.

Scene uses three rounded panels with standard grey bodies and dark headers: object list on the left, interactive preview in the centre, compact position/rotation/scale fields on the right. Move, Rotate and Scale sit above the preview, alongside Navigate and Frame. A pencil beside editable objects and an Edit source context-menu action open the shared Editor tab. Files and Lua effects use that same tab, retaining the existing font, slider, console and help controls. Returning to Scene preserves selection and framing.

The existing plus button and Examples browser handle scene additions, including live sources. The browser visibly changes its heading to Add to scene. Switching to Effects ends this import context; normal file opening then creates another selectable scene. Dropping a source into the scene preview also adds it to the scene.

Full 3D placement and free camera movement remain essential. Each scene has one camera shared by editing and output. The geometry preview uses the existing Perspective/FOV settings; the visualiser alongside it shows the final effects. Guides and handles never appear in recordings or texture output. A separate inspection camera, lighting and material editors are outside this version.

## Automation without restricting scene size

The fixed resource is the number of **exposed entities**, not the number of objects in the project. An entity is either one object or one scene camera. Right-clicking it and choosing **Expose to DAW** assigns one free slot from the project-wide pool.

Each object slot exposes a group of transform parameters, rather than requiring a separate exposure action for every axis. Cameras use the same exposure mechanism and consume slots from the same pool. The exact camera parameter set and total slot count remain implementation decisions. Nothing is exposed automatically.

With an illustrative pool of eight slots:

| Assignment | Slots consumed |
|---|---:|
| Eight exposed objects in one scene containing 100 objects | 8; no slots remain anywhere in the project. |
| One object in scene A, two in scene B and five in scene C | 8; no slots remain. |
| Three objects and the camera of scene A | 4; four slots remain for any scene. |
| Additional objects that are not exposed | 0 additional slots. |

Eight is an example, not an agreed limit. A scene with many objects may be expensive to render, but exposure capacity must not determine whether those objects can exist.

The host sees a fixed parameter inventory. Bindings saved in the project connect those parameters to stable object/camera identities. Reordering objects, changing scene or switching panels must not retarget automation. Internal control state continues to use the project’s parameter system; exposed slots provide the host-facing bindings, not a replacement with ad hoc plain values.

Recommended engineering rules, to finalise during implementation:

- Keep bindings attached to identity rather than list position or current selection.
- Define deletion and explicit unexposure so existing automation cannot silently start controlling another object.
- Store and restore bindings with the scene/object state.
- Define inactive-scene handling so returning to a scene produces the expected automated state.
- Keep stable host IDs and ranges, with appropriate conversion to object or camera controls.
- Forward editor gestures for exposed controls through their host-facing parameters.

Future per-object effects will need a further exposure design. They do not need to fit into an unlimited predeclared rack in this first version.

## Playback

All animated objects in the active scene follow one scene time. The existing global speed control changes that time’s rate; scrubbing moves the objects together. Do not add individual speed or offset controls.

At ordinary speed with looping enabled, a two-second GIF repeats while a five-second GIF continues through its own animation. Both use the same elapsed scene time, each evaluated against its own duration. They do not get stretched to finish together.

Preserve current host sync, file-switching and playback behaviour wherever applicable. The decision is to retain that behaviour, not to choose a new universal “restart on selection” or “continue in the background” policy without checking the implementation. Record the existing behaviour in regression cases before changing the selection system.

The displayed scrub range for a scene containing sources of different lengths still needs a simple implementation rule. This is not a reason to introduce arrangement tracks or per-object transport controls. Live inputs should retain their existing live-time behaviour rather than pretend to seek with stored animation.

## All existing sources participate

Do not restrict the initial release to static SVG, OBJ or text. Current animated, procedural and live sources must also work inside a scene. Reuse their existing 3D point outputs and transform conventions; retain depth where the source already supplies it.

A common point type does not mean every source has the same lifecycle. Stored geometry, decoded animation, procedural Lua and live inputs differ in when they produce data and whether they can seek or be cached. Preserve those capabilities behind the source/object boundary instead of treating every source as a static mesh.

Source-type parity does not by itself specify unlimited simultaneous live connections. Existing Blender connection and texture-input ownership need inspection to determine how they attach to an object. Connection multiplicity, independent Lua state and decoding lifetimes are engineering questions to resolve, not reasons to silently drop those source types from version one. [L2][L3]

## Remaining engine work

The interface is an extension, but composing source signals still requires deliberate engine work. Adding two XY audio signals creates a new trajectory; it does not generally display the two original objects side by side. The composition implementation must preserve the intended separate objects through an appropriate drawing strategy.

Evaluate representative mixed sources before settling the scheduling policy. Sampling time, travel between objects and polyphony affect both detail and sound. At 48 kHz and 440 complete scene cycles per second there are roughly 109 samples per scene cycle, before dividing work among objects. This is an illustrative sampling budget, not a proposed product limit.

Do not prescribe a new set of user-facing drawing modes, allocation sliders or a quality inspector as part of the editor just because these engineering questions exist. Aim for an understandable default that works with existing controls. Performance limits should be handled honestly without promising arbitrarily complex scenes will render perfectly.

Preparation, parsing, decoding and expensive composition work must remain off the audio thread. Use bounded processing and prepared buffers even though there is no small fixed UI limit on total objects. Preserve the existing realtime conventions and visualiser semaphore logic.

## What is outside version one

- Per-object creative effects, repeated effect instances and reusable racks.
- Different effect chains or recalled effect values per scene.
- Snapshots, scene morphing, transition designers and performance macros.
- Individual animation speeds, offsets or timelines.
- A separate inspection camera or multiple cameras within one scene.
- Group hierarchies, parenting, spatial constraints and node editing.
- A new drawing/illustration system, marketplace or preset-browser redesign.
- A DAW-style arrangement timeline.

These remain independent future possibilities. The first-version architecture should leave useful boundaries for later work without implementing those features now.

## Implementation checks

| Check | Expected result |
|---|---|
| Existing single-source workflow | Main controls and global effects behave as before; the Scene panel is optional. |
| Normal open versus editor import | Normal opening follows current selection flow; importing inside the editor adds to the current scene. |
| Cube and text | Both can be positioned, rotated and scaled directly in shared 3D space. |
| Two scene cameras | Framing is saved per scene, and navigating the editor camera changes actual output. |
| Shared effects | Switching scenes or selecting objects does not replace or recall the project-wide chain. |
| Every current source type | Stored, animated, Lua, audio and live sources participate with their existing dimensional semantics. |
| Different animation lengths | Global speed and scrub act together; looping preserves each source’s own duration. |
| Scene switching | Existing playback behaviour is preserved, with no additional preference. |
| Exposure across scenes | Object and camera assignments share one pool and survive switching, reordering and save/reload. |
| Many unexposed objects | Objects can exist beyond exposure capacity; resource behaviour remains bounded and predictable. |
| Closed editor | Rendering and automation continue without depending on visible editing controls. |
| MIDI/polyphony | Existing voice/effect behaviour is retained while composition produces the intended output. |

The immediate implementation investigations are preview/picking for all source types, composition scheduling, existing switch/playback semantics, camera integration with current Perspective controls, and the exact exposed parameter groups. None requires another large product-design layer.

## Local references

These references anchor the implementation questions; they do not imply that the proposed scene feature is already implemented.

- **L1:** [PluginProcessor](../../Source/PluginProcessor.cpp) and [ShapeVoice](../../Source/audio/synth/ShapeVoice.cpp): effect registration, per-voice processing and output.
- **L2:** [FileController](../../Source/FileController.h), [selection implementation](../../Source/FileController.cpp), and [ShapeSound](../../Source/audio/synth/ShapeSound.h): source ownership, switching and frame delivery.
- **L3:** [Point representation](../../modules/osci_render_core/shape/osci_Point.h), [FileParser](../../Source/parser/FileParser.cpp), and [image parser](../../Source/parser/img/ImageParser.cpp): common geometry and differing source behaviour.
- [Animation timeline controller](../../Source/components/timeline/AnimationTimelineController.cpp): existing animation controls and timing.
- [Overlay component](../../modules/osci_gui/components/osci_OverlayComponent.h): existing in-app overlay infrastructure.

The [main research report](feature-research.md) retains the external evidence and wider feature opportunities. This document is the narrower product specification.
