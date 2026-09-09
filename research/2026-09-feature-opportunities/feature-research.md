# osci-render feature opportunities

**The strongest next step is to make osci-render better at producing a complete, personal, performable visual instrument.** Multi-object scenes are the largest opportunity. Reusable looks, macros, richer audio reactivity, and a strong library would make that capability accessible and give premium a clearer reason to exist. Better capture, recovery, and everyday editing should accompany those additions.

This report covers physical oscilloscope artists, DAW musicians, video creators, and live performers. Visual workflows receive particular attention because a recognisable visual result is easy to demonstrate and share. The proposals preserve osci-render’s connection between geometry and sound. Arrangement timelines and laser output are outside scope.

The companion [Multi-object scenes](scene-composition.md) document now defines the agreed first version: an optional 3D editor overlay, one project-wide effects chain, per-scene cameras, shared playback controls and a project-wide pool of exposed objects/cameras. It takes precedence over the broader scene opportunities below. Snapshots, macros and per-object effects remain separate future features. Feature IDs below are stable references for choosing future work.

## Priority shortlist

Confidence describes expected usefulness to the intended audience, not guaranteed sales. Effort is relative: **S** is a local change, **M** spans several components, **L** introduces a substantial subsystem, and **XL** changes the engine or product architecture. These are planning judgments, not line counts or delivery estimates.

| Priority | Feature | Confidence | Effort | Why it deserves attention |
|---|---|---|---|---|
| 1 | **M01 Multi-object scene composition** | Very high usefulness; medium delivery certainty | XL | Makes a logo, text, animation, and framing elements into one controllable composition. |
| 2 | **M02 Performance snapshots** | Very high | L | Revisit good results instantly; a direct user request and a foundation for live use. |
| 3 | **M03 Named macros and a performance panel** | Very high | M–L | Converts a complicated patch into a playable instrument with a handful of meaningful controls. |
| 4 | **M04 Visual preset browser and curated projects** | High | L | Helps people reach an impressive result and creates a practical route to premium content. |
| 5 | **M05 Frequency-band and transient modulation** | High | L | Makes existing music drive different visual behaviours without preparing MIDI. |
| 6 | **M07 Accessible colour and stroke styling** | High | M–L | Makes the existing RGB capability useful beyond custom scripting. |
| 7 | **M08 Stable contour conversion for images/video** | High visual value; medium technical confidence | L–XL | Improves recognisability and consistency of imported real-world content. |
| 8 | **M09 Shape morphing with correspondence controls** | High appeal; medium technical confidence | L–XL | A very strong demo feature if common inputs produce reliable transitions. |
| 9 | **M12 Loop capture and sampler handoff** | High | M–L | Turns a good experiment into reusable musical or visual material. |
| 10 | **M15 Recovery and dependable project handling** | Very high usefulness; indirect commercial value | M–L | Protects the work that makes users return. |

A sensible first commercial package is **snapshots + macros + excellent example projects**, while scene-engine feasibility is investigated. Scenes are the longer-term headline. This avoids making all near-term progress depend on a large architectural change.

## Current capability baseline

The published GitHub release inventory contains 153 entries, from v1.0 to v2.8.9.18. The public release list is behind the local checkout, whose HEAD is 14c7f98c and whose project version has reached 2.9.7.3. Public release tags, current development code, and local release notes therefore need to be distinguished. Early release bodies are sparse; the v1.33.2 bundled changelog provides the useful historical account. [^1][^2]

| Period | Established capability relevant to this research |
|---|---|
| Early v1 | OBJ/SVG/text-to-audio, transformations, recording, MIDI and oscilloscope-oriented effects. |
| v1.21–1.27 | Hidden-edge processing, Blender input, software scope, Lua creation/effects, project selection and installed fonts. |
| v1.28–1.33 | Broader animated controls, microphone/audio input, resizable UI, additional effects and MIDI refinements. |
| v2.0–2.4 | JUCE/plugin workflow, DAW automation, GPLA, GIF/image/audio imports, Lua helpers and realistic software rendering. |
| v2.6–2.8 | Larger effect suite, improved synth and modulation, per-voice effects, MTS-ESP, RGB paths, system audio capture, offline video rendering and undo/redo. |
| Current local 2.9 code | Lottie/video/texture workflows, rectangular canvases, transparent recording/popouts, Program Change/file actions, routing refinements, installer/update/account and feedback features. |

The historical inventory is context, not a claim that every historical implementation survived unchanged. Current code is the stronger source for whether a proposed feature would be new. [^1][^2][L1]

**Ideas deliberately removed or narrowed after checking the code:**

- Basic video/Lottie import, Syphon/Spout, transparent ProRes recording, portrait canvases, offline audio-to-video rendering, Program Change, MIDI channel selection, axis inversion, and popout pinning already exist.
- The current modulation system already has drawable LFOs, envelopes, random sources, a sidechain transfer curve, and LFO preset browsing. “Add modulation” is not a useful recommendation.
- Randomisation exists and changes selected effects, values, and order. The opportunity is controlled variation, not a second random button.
- OBJ routing already uses Chinese Postman processing. A new quality feature must address measurable remaining problems, other source types, or temporal consistency.
- Duplicator and Multiplex repeat an existing signal. They do not provide independently editable imported objects.
- Project assets are already embedded in state, and a portable snapshot path exists. “Collect all and save” alone overstates the missing work; dependency inspection and reusable bundles are more precise opportunities.
- Local uncommitted changes include performance wheels. Basic pitch/mod-wheel controls are treated as underway, not proposed as new work.
- Closed issues can contain unfinished subrequests, and open issues can describe functionality now present. Issue status alone is not evidence of a current gap. [L1–L8]

## Competitive context and evidence of need

| Reference | Verified capability or signal | Implication for osci-render |
|---|---|---|
| OsciStudio | Converts geometry to sound, supports live coding and parameter timelines. Its forum also documents multi-object controls. [^3][^13] | The direct comparison should be ease of building and performing sound-images, not timeline parity. |
| Imaginando VS | Eight blendable visual layers, polyphonic visual voices, modulation, media sources and video output. [^4] | Editable composition is an understandable expectation among visual-instrument users. |
| Synesthesia | Audio-responsive scenes, a substantial starting library and an integrated scene marketplace. [^5] | Content and immediate musical response can matter as much as raw engine features. |
| Magic Music Visuals | Modular composition with multiple sources, scenes and separate preview facilities. [^6] | Source mixing and preview are proven workflows; a full node environment is not required to capture their value. |
| ZGameEditor Visualizer | Layers, a title/author template wizard, mixer-source selection and export presets. [^7] | osci-render competes with convenient finished-video workflows already inside a DAW. |
| Resolume | Dashboard controls can drive several parameters; texture and network output connect applications. [^8][^9] | Macros and interoperability let osci-render participate in a larger rig. |
| Vital / Pigments | Modern synths foreground visual modulation and expressive control; Pigments includes assignable macros. [^10][^11] | Strong synthesis UX transfers well without adding another arrangement system. |
| PsyScope / Blue Cat | Multitrack comparison and analytical displays are their explicit focus. [^12] | A full measurement suite would open a different competitive battle; shared creative display features deserve priority. |

There are unusually useful first-party demand signals. Issue #293 asks for effect-state recall, MIDI/hotkey access, and transitions; its author describes a cumbersome DAW preset workaround. Issue #156 asks for multiple objects and morphing. Issue #286 asks to choose a recording destination before capture. Issue #154 describes lost work from incomplete unsaved-change handling. These are individual requests, not a representative survey. [^14][^15][^16][^17]

The wider community provides supporting context. An OsciStudio user wanted one object traced while another remained solid; the developer confirmed that the described workflow was unavailable in 2021. A later discussion describes awkward animation reuse. These are dated workflow observations, not proof of the competitor’s current limitations. [^18][^19]

Physical-scope discussions repeatedly involve distorted output and setup confusion. Other posts describe the difficulty of keeping attractive geometry musical, exporting usable loops, and finding reusable scripts. Those are reasons to prototype clearer workflows; they do not establish market size or willingness to pay. [^20][^21][^22][^23]

## Major features

### M01 — Multi-object scene composition

**Very high usefulness · XL · osci-render · strongest premium headline.**

Import several sources and arrange them directly in an optional 3D scene-editor overlay. Keep the main interface and existing project-wide effects chain unchanged. Each scene has its own output camera; the existing file arrows, File Select automation and Program Change become scene selection. This addresses the composition part of #156; morphing and independent creative effects remain future work. [^15]

The agreed first version supports every current source type, independent object transforms, free camera movement and one shared playback clock for the active scene. Global speed/scrubbing affect its animations together, with each animation looping at its own duration. Preserve current switching behaviour.

There is no fixed object-count limit imposed by automation. Explicitly exposing an object or camera consumes one slot from a fixed project-wide pool; each slot exposes a group of controls and stays bound across scene changes. The exact slot count is undecided. Per-object effects, per-scene effect recall, snapshots and macros are not first-release requirements. See the dedicated specification for remaining engineering questions, including drawing composition and integration with current voice/effect semantics.

**Validation:** ask artists to reproduce a three-element composition they currently assemble externally. Judge time to a useful result, intelligibility on software and hardware scopes, and whether they choose it for an actual project.

### M02 — Performance snapshots with deliberate transitions

**Very high · L · osci-render; shared visual settings where applicable · premium.**

Save named states such as “clean”, “fractured”, and “wide”, then recall them from a compact strip, MIDI, or a DAW parameter. This is broader than File Select: it includes the effect values and modulation configuration that make a look. Start with recall within a fixed asset/effect topology; add transitions only where their meaning is defined. [^14]

Allow capture scopes: selected effects, the instrument, or visualiser settings. Avoid recalling hardware routing or a capture destination by accident. Continuous values can interpolate with appropriate parameter scales; file changes, effect types and toggles need explicit switching rules. Preload or prepare state off the audio thread.

**Commercial demonstration:** build a complete performance from three saved looks, with no plugin reload. **Validation:** recall repeatedly under host automation and confirm that intended state and output are reproducible.

### M03 — Named macros and a focused performance panel

**Very high · M–L · primarily osci-render · premium mapping depth.**

Offer perhaps eight named controls, with user-defined ranges, inversion and response curves, plus an XY pad. One “Bloom” knob might expand geometry, open a trace, and change colour; “Tension” could increase deformation while reducing persistence. Expose stable macro parameters to the DAW. Resolume’s dashboard provides a relevant interaction precedent. [^8]

Use these same controls in factory projects, so a beginner encounters meaningful musical or visual actions instead of an unexplained wall of parameters. A pinned parameter is a shortcut; a macro that coordinates several destinations is a compositional tool. This can ship before scenes.

**Validation:** can a new user perform an unfamiliar preset without opening its effect chain? Can the creator bind the intended destinations without consulting documentation?

### M04 — Visual preset browser and curated project library

**High · L · shared browsing infrastructure · premium projects and optional packs.**

Provide searchable thumbnails, favourites, tags, author information, and clear source requirements. Browse complete projects, reusable effect setups and, later, scene objects. Existing bundled examples and LFO presets are foundations, not a complete project discovery workflow. [L3][L4]

Organise by outcomes: “playable bass”, “clean logo”, “reactive typography”, “physical-scope friendly”, “transparent overlay”, and “live performance”. Audition previews without unexpectedly replacing the current project or sounding a loud patch. Explain which examples require premium.

VS sells themed expansions, and Synesthesia supports scene purchases. That establishes a viable category of product, not a revenue forecast for osci-render. Start with a small excellent included collection and a manually distributed creator pack; a marketplace can wait. [^5][^24]

### M05 — Frequency-band, transient and spectral modulation

**High · L · osci-render first; selected shared visual controls later · premium.**

Add low/mid/high envelope sources, a configurable band, and a transient pulse, with easy sensitivity calibration and visible activity. A drum track could open a shape on kicks, disturb its outline on snares, and colour it with high-frequency energy. The present sidechain is one smoothed level source, so this is a substantive improvement. [L2]

Start with deterministic, understandable analysis. Pitch tracking, beat inference and semantic “vocal detection” should be optional later experiments with confidence and latency shown. Synesthesia demonstrates the appeal of automatic reactivity, but its promotional claims should not be treated as an accuracy benchmark. [^5]

**Validation:** use quiet, compressed, sparse and dense tracks. Assess whether mappings remain controllable without continual gain adjustment, including offline rendering and silence.

### M06 — Several independently assignable audio inputs

**High for DAW and live users · L · primarily osci-render · premium.**

Let drums drive one visual property while vocals or bass drive another, through named sidechain buses or supported standalone channels. Include a small signal monitor and source selector at the modulation source. Magic and ZGameEditor provide relevant multiple-input and mixer-source workflows. [^6][^7]

This is distinct from M05: frequency bands from a full mix do not isolate stems. Begin with a small supported bus configuration and published DAW setup examples. Do not promise arbitrary routing in every host, and avoid an elaborate internal mixer.

**Validation:** complete the same two-stem task in the main supported hosts without helper applications or ambiguous bus names.

### M07 — Colour palettes and stroke styling without code

**High · M–L · shared visual styling; object-specific controls in osci-render · premium.**

Make per-object colour, palettes and gradients accessible through ordinary controls. Map colour along path progress, by object, by note, or from a selected audio band. Add restrained controls for stroke appearance that remain recognisable as oscilloscope imagery. RGB point transport and Lua colour already exist; the gap is authoring and discovery. [L1][L5]

Keep geometry coordinates, depth, intensity and colour semantically distinct. State clearly which styling affects the software output and which survives a particular recorded or physical signal path. A basic two-channel scope cannot display independent RGB just because the preview can.

**Demo:** one editable logo becomes an animated colour identity with a few controls. **Validation:** colour follows the expected object through effects, export and snapshot recall.

### M08 — Stable image/video contours

**High visual value · L–XL · osci-render · premium.**

Offer a contour-oriented alternative to the existing brightness-driven image sampler: silhouettes, clean edges, minimum feature size, detail budget and temporal stabilisation. Preserve recurring contours across video frames to reduce distracting rearrangement. The useful claim is “your logo or camera subject stays recognisable while moving”. [L6]

Bitmap-to-vector tracing is established; Potrace is an algorithmic reference. Stable animated correspondence is additional work, not something a still-image tracer solves automatically. Start with high-contrast logos and simple silhouettes, then broaden the input set. Direct webcam capture is a follow-on convenience; texture input already covers externally supplied live video. [^25]

**Validation:** compare against the current parser on a labelled set of logos, faces, line animations and noisy footage, at equal output sample rate and comparable detail.

### M09 — Shape morphing with controllable correspondence

**High appeal · L–XL · osci-render · premium.**

Morph between two paths or compatible objects using an automatable control. Include endpoint preview, start-point adjustment, reversal and a small number of manual correspondence anchors. An artist should be able to fix an unfortunate pairing instead of accepting a tangled intermediate shape. #156 explicitly includes morphing. [^15]

Ship constrained cases first: primitives, single contours, and compatible path collections. Arbitrary SVG text, holes, disjoint paths and differently connected meshes are harder. Flubber’s documented limitations illustrate why generic two-dimensional interpolation is not a finished oscilloscope morphing engine. [^26]

**Validation:** evaluate every intermediate state, not just the endpoints. Listen for discontinuities as well as inspecting geometry. Distinguish geometry interpolation from a signal crossfade and from a change in drawing allocation.

### M10 — Audio-through-shape waveshaping

**High relevance · L · osci-render · premium instrument/effect.**

Use incoming audio to scan a selected shape, with input range, offset, smoothing and dry/wet controls. A vocal or instrument becomes a visible contour while retaining some audible character. This is a concrete synthesis direction with both an existing community implementation and an osci-render request. [^27][^28]

WavShaper demonstrates the basic approach and notes that complex shapes reduce intelligibility. osci-render’s opportunity is native source selection, animated shapes, preset design and high-quality integration—not claiming the technique is new. An attractive visual result must not be sold as transparent audio preservation.

**Validation:** compare speech intelligibility and musical usability across simple and complex shapes; measure level changes and aliasing, and provide a predictable bypass.

### M11 — Drawing-quality inspection and targeted optimisation

**High usefulness for ambitious sources · L · osci-render · premium advanced tools.**

Show where the beam spends time, where it jumps, which details are undersampled, and how much of a cycle is spent travelling. Offer specific repairs: simplify tiny features, reduce redundant retracing, stabilise route order and rebalance detail. Preserve a before/after preview and allow the original traversal to be kept for its sound.

This extends existing OBJ path work; it does not replace it with an unspecified “optimiser”. Geometry traversal influences both image and timbre. A shorter route is not automatically a better musical result. [L7]

**Validation:** benchmark imported scenes and animated sources. Score recognition, flicker, discontinuities and spectral changes separately. Software-only supersampling must not imply extra detail survives a lower-rate physical output.

### M12 — Exact loop capture, retrospective audio and sampler handoff

**High · M–L in stages · shared capture where applicable · premium convenience.**

Make “keep that” a first-class workflow. Capture a selected number of cycles or bars, drag the result into a DAW/sampler, and retain enough source state to revisit it. A bounded retrospective audio buffer can rescue an experiment that happened before Record was pressed. Historical user discussions specifically describe trouble making stable loops for sampling. [^22]

The inspected recording path is button-driven; a closed request also asks for automatable start/stop. Include transport-scheduled capture in this scope rather than assuming it already works. Add seam diagnostics and direct handoff. [^38][L9] A musically exact bar length does not guarantee a geometric seam, and persistence or nonperiodic modulation can prevent a seamless video loop. Report that honestly and offer a preview rather than silently applying an XY crossfade.

**Validation:** import the result into common samplers, play it over several repetitions and confirm there is no unexplained seam, metadata mismatch or lost stereo information.

### M13 — Reactive typography and title templates

**High for visual creators · M–L · osci-render · premium richer controls.**

Add proper line spacing, alignment, character spacing and a few tasteful per-character behaviours: reveal, scatter, orbit and settle. Let a DAW parameter or MIDI select a line of text from a prepared list. This supports names, titles, short phrases and stage identities without becoming a subtitle editor.

ZGameEditor demonstrates the value of editable title templates. osci-render should make them distinct through beam-drawn type and sound-linked movement. Start with simple, readable single-line designs; offer a single-stroke font collection to reduce drawing cost. [^7]

**Validation:** replace the title in a preset with a longer name. It should remain usable without manual rework, and unsupported glyphs or substituted fonts should be obvious.

### M14 — Native primitives and lightweight drawing

**High for primitives; medium for a general editor · M then L · osci-render.**

Create a circle, polygon, line, star, spiral or freehand stroke without preparing a file. Keep primitive parameters editable and automatable. A small pen/path editor could subsequently support start-point placement, reversal and selected-node correction.

The valuable initial scope is “make a simple element in ten seconds”. A full illustration program would be a much larger commitment. Native primitives also provide dependable examples for scenes, morphing, geometry effects and calibration. Lua helpers already generate shapes; the new value is direct manipulation and reusable object controls. [L1][L3]

**Validation:** beginners assemble a simple title frame without using another application or writing code.

### M15 — Recovery, unsaved-state handling and asset inspection

**Very high usefulness · M–L · shared · keep core protection free.**

Track meaningful project edits, provide reliable save prompts in standalone, and keep bounded recovery snapshots after crashes. Add an asset inspector showing embedded files, size, unused content and external dependencies. A reopen-without-heavy-media option would address the confusion described in #280. [^17][^29]

DAW hosts own plugin project saving; recovery must respect host state and avoid surprise file prompts. Existing embedded assets and portable snapshots should be reused. Dependency inspection matters particularly for fonts and future packaged scripts. [L8]

**Validation:** reproduce interrupted saves, missing dependencies, large media and unsaved parameter changes. Confirm recovery restores the intended project without altering its original.

### M16 — Spatial modifiers and reusable motion behaviours

**High fit; medium demand evidence · L · osci-render · premium.**

Add a small set of composable behaviours: follow a path, orbit a parent, look at a target, repeat along a curve, and affect points within a geometric region. This makes controlled designs possible that are awkward with only global distortions. It is especially useful once objects have identities.

A “falloff” could limit a twist to one end of a shape; a path-follow control could move a label around a ring. These should be ordinary understandable controls first. A full node graph is a later interface decision, not a prerequisite.

**Validation:** artists reproduce a specific intended movement rather than merely discovering random attractive accidents.

### M17 — Packaged Lua instruments and effects

**High for creators; medium breadth · M–L · osci-render · premium content opportunity.**

Allow a script to declare meaningful control names, ranges, help text, a preview and asset dependencies, then install it as a reusable instrument or effect. Add a clear “apply/run” workflow and retain the last working version when edits fail. Existing documentation, variables and custom sliders are substantial foundations. [L3]

Separate confirmed remaining gaps from the older Lua wishlist: several requests there have already been fulfilled. Investigate multiple reusable scripted effects and independent controls instead of assuming Lua itself needs replacing. [^30]

**Validation:** a creator distributes a useful instrument to someone who never opens the code editor. They should be able to understand its controls and restore its defaults.

### M18 — OSC control and standalone Link synchronisation

**High for performers; medium broad demand · M–L per integration · shared where meaningful.**

Expose a small documented OSC surface for macros, snapshots, source selection and transport-related actions. Supply a TouchOSC example. Separately, offer Link tempo/phase synchronisation in standalone, while host timing remains authoritative in a plugin. Ableton documents these synchronisation concepts explicitly. [^31]

These should be separate deliverables; neither is a reason to build a sequencer. A visible connection state and discoverable control addresses matter more than an enormous raw API.

**Validation:** run a controller and a separate visual app through join/leave, reconnection and tempo changes. Do not equate beat synchronisation with sample-synchronised audio devices.

### M19 — NDI output and clearer external-output setup

**High for some visual rigs; medium overall · L · shared · premium.**

Add network video output after confirming actual user demand beyond existing same-machine texture sharing. Provide named destinations and a small setup check for output dimensions and alpha support. Resolume and Synesthesia demonstrate NDI’s role in multi-computer visual workflows. [^5][^9]

Start with output only; add input if a concrete workflow needs it. Cross-platform SDK integration, distribution terms, performance and end-to-end alpha behaviour need a feasibility check. A claim that an exporter has an alpha channel is not proof the receiving pipeline preserves it.

**Validation:** send a representative scene to a second computer for a sustained performance, measuring latency and continuity.

### M20 — Physical-scope setup and calibration profiles

**High for hardware users · M for guidance, L for compensation · osci-render/shared signal tools.**

Provide test patterns and a guided sequence for X/Y identification, aspect, gain and channel timing, with named profiles. Existing inversion and swapping remain the basic controls. Community setup problems support making this workflow understandable. [^20]

Begin with diagnostics and manual adjustment. Automated camera matching or inverse filtering is a later experiment. Do not promise to undo lost DC content, clipping, limited bandwidth or every cable/interface problem. The best tool can sometimes explain why the setup cannot reproduce the source.

**Validation:** novice users correctly identify an intentionally introduced swap, gain mismatch or timing mismatch and can restore their previous configuration.

### M21 — Expressive MIDI beyond the current wheel work

**High for expressive performers; medium broad impact · L · osci-render · premium.**

Expose note number, velocity and pressure as convenient modulation sources, then evaluate MPE voice expression. Pressing harder could open an individual shape; sliding could deform it. Keep this separate from the local pitch/mod-wheel implementation already underway. MPE’s per-note expression is documented by the MIDI Association. [^32][L2]

Visible separation of notes on a physical scope is a separate scheduling problem; expression alone does not solve it. CLAP has an explicit request and useful per-note facilities, but format support should follow a host-demand check rather than be called a small feature. [^33]

**Validation:** test actual controllers and host recordings, including voice stealing and overlapping notes, rather than only sending synthetic CC messages.

### M22 — Reusable effect racks and repeated effect instances

**High compositional value · L–XL · osci-render · premium.**

Save a named chain with its modulation, reuse it on another source, and eventually instantiate the same effect more than once. A gentle deformation followed by a second deformation after another transform is a different design tool from a single instance with more controls.

The present effect list has one built instance per type and disables already selected grid entries. Duplication therefore reaches parameter identity, serialisation, modulation and voice cloning; it is not a context-menu-only change. Coordinate this design with per-object effects rather than building incompatible rack systems. [L4]

**Validation:** duplicates have independent values, automation and undo, and survive save/reload without cross-contamination.

### M23 — Render jobs and export variants

**High for frequent video creators · M–L · shared · premium.**

Queue existing audio-to-video jobs, save output recipes, and export the same performance in multiple aspect ratios with intentional framing. Include previewable crop/fit choices and a still image for a cover. Rectangular canvases and offline rendering already exist; the new value is repeatable delivery without repeated manual setup. [L1]

Start with offline audio sources and immutable visualiser state. Rendering a full plugin performance with MIDI and automation outside the host is a much larger scope and should not be implied by “render queue”.

**Validation:** compare a job with its interactive equivalent and confirm cancellation, destination conflicts and subsequent jobs behave predictably.

### M24 — Export the generated vector result

**Medium confidence · L · osci-render and selected sosci capture · premium.**

Export a selected generated path or a deliberately bounded sample interval as SVG, enabling print, plotter and motion-design workflows. Distinguish source-geometry export from a captured audio trace. Exporting the original imported SVG again is not this feature.

A community request asks to move a scope-generated result into Illustrator, while also exposing the complication: continuous audio has no intrinsic frame boundary. The workflow needs explicit time/cycle selection and simplification controls. [^34]

**Validation:** reopen the exported vector elsewhere and compare it with the selected interval, including disconnected segments and effect-generated geometry.

### M25 — Animated glTF/GLB import

**Medium confidence · L–XL · osci-render · premium.**

Support a bounded glTF subset for portable animated scenes, cameras and named objects. glTF defines those structures, so it is a more coherent first target than adding many unrelated 3D formats. Preserve scene hierarchy where possible and give users control over the extracted lines. [^35]

Existing Blender and GPLA workflows already solve substantial animation needs. Prioritise this only if potential users are blocked by that workflow, or once scene composition can retain the imported structure. Skins, morph targets, materials and extension support must be scoped explicitly.

**Validation:** a small published set of supported exports loads consistently, with useful line density and correct animation timing.

### M26 — Shared multi-source visual comparison

**Medium confidence · L · sosci and shared display · secondary.**

Show several named audio traces in one software view, with colour, visibility and saved display arrangements. This could be useful for recording a performance’s parts or demonstrating sound design. Analytical competitors establish multitrack display as a real use case, but a complete spectrum/loudness/measurement suite is not the priority. [^12]

This is software composition, not a claim that adding the source signals creates the same image on one physical scope. Cross-instance discovery also needs to account for DAW process isolation. Prefer explicit inputs before inventing a fragile hidden-routing system.

**Validation:** a user can identify and capture the intended sources without routing ambiguity. Demand should be demonstrated before it displaces shared creative features.

## More speculative, distinctive directions

These are original product proposals or combinations of established techniques. Novelty has not been established, and none should be marketed as unprecedented without a separate prior-art investigation.

| ID | Idea | Why it could be valuable | First experiment and stop condition |
|---|---|---|---|
| X01 | **Timbre-preserving geometry exploration** | Offer several traversals of a shape that approach a chosen harmonic character. Makes the sound/image compromise an explicit creative control. | Optimise a small set of simple closed paths for a spectral target while constraining geometric error. Stop if the improvement is negligible outside hand-picked examples. |
| X02 | **A playable bank of drawing fragments** | Scan and retrigger strokes or sections of a shape like a sampler, creating rhythmic marks and new timbres. | Use known closed fragments and smooth boundary policies. Stop if almost every result requires manual repair or is indistinguishable from existing Trace modulation. |
| X03 | **Geometry-to-modulation sources** | Centroid, contour density, distance to a target or curvature drives colour/effects. Imported motion becomes a source of control. | Compute descriptors from a previous prepared frame to avoid cyclic dependencies. Test whether artists can intentionally use the result. |
| X04 | **Harmonic shape families** | A collection of geometrically related patches is designed around useful bass, lead and percussion sounds; moving within the family preserves a coherent visual identity. | Commission a small pack and measure actual reuse. This may be a stronger content product than an additional engine. |
| X05 | **Guided variation browser** | Generate several small, reversible alternatives while respecting locked parameters, source geometry and output bounds. Users select a direction and continue. | Begin with constrained parameter mutation and cached previews. Stop if ranking/filtering does not beat the existing randomise-and-undo workflow. |
| X06 | **Geometry-aware transition vocabulary** | Assemble, orbit apart, trace out, collapse along a contour, and exchange strokes provide transitions that belong to oscilloscope art. | Implement two transitions on compatible objects using scene snapshots. Stop if they require a general animation editor to be useful. |

X01 deserves genuine research but should not delay M10, which has a concrete precedent. X04 and X05 have relatively cheap ways to test whether their appeal survives a real creative session.

## Smaller improvements

These are practical additions rather than promised sub-100-line changes. **S** and **M** remain relative effort estimates; audio/host state, undo and export threading can make a tiny UI affordance nontrivial. “High” means useful within the relevant workflow. Items marked “later” depend on a major feature. Unless a row says otherwise, these are proposals inferred from the inspected code rather than requests proven by a user study.

### Editing and discovery

| ID | Improvement | Confidence / effort | Concrete scope |
|---|---|---|---|
| Q01 | Search the effect picker | High / S | Filter by name and a few plain-language tags while retaining existing hover previews. |
| Q02 | Favourite and recently used effects | High / S–M | Small local lists within the picker; avoid a new top-level menu. |
| Q03 | Search the current file list | High / M | Thumbnail/name navigation when a project contains many visuals. |
| Q04 | Replace a source while retaining its settings | High / M | Explicitly preserve transforms and mappings; unlike adding or duplicating a file. |
| Q05 | Copy/paste an effect’s settings | High / M | Transfer values with an explicit choice about modulation assignments. |
| Q06 | Copy/paste a parameter value | High / S | Validate and clamp numeric input; Set Value and Reset already exist. |
| Q07 | Reset an entire effect | High / S–M | One undoable operation; decide whether modulation is retained. |
| Q08 | Collapse all / expand selected effects | High / S–M | Keep long chains navigable without removing their controls. |
| Q09 | A named destination list with jump-to-control | High / M | Extend existing depth indicators and hover highlighting with a readable list that can scroll directly to each assigned control. |
| Q10 | Show base and modulated values together | High / M | Clarify why an animated parameter differs from the stored value. |
| Q11 | Clear mappings for one destination | High / S–M | A focused action with undo, not only a global clear. |
| Q12 | Lock values during randomisation | High / M | Preserve chosen effects, ordering or parameters when exploring. |
| Q13 | Add a small variation amount | High / M | Perturb current values within bounds, instead of selecting a new whole chain. |
| Q14 | Temporary A/B comparison | High / M | Compare two states within the current topology; larger snapshot system can follow. |
| Q15 | Pin frequently used parameters | High / M | A compact user-selected control strip, distinct from multi-target macros. |
| Q16 | Browse examples by goal | High / S–M | Describe what each example teaches and which controls to try first. |

### Capture and project handling

| ID | Improvement | Confidence / effort | Concrete scope |
|---|---|---|---|
| Q17 | Choose capture destination in advance | High / M | Named destination and default filename pattern; still requested in #286 and current recording chooses after stopping. [^16][L9] |
| Q18 | Drag the last recording into another app | High / M | Expose the completed file as an OS drag, retaining the existing saved file. |
| Q19 | Export a still / copy preview image | High / M | Capture the actual rendered frame at selected resolution, not a UI screenshot. |
| Q20 | Named export recipes | High / M | Save codec, dimensions, frame rate and audio choices together. |
| Q21 | Show aspect/crop guides | High / S–M | Guides are preview overlays and do not enter the exported image. |
| Q22 | Export progress, destination and completion action in one place | High / M | Make “where did it go?” answerable without another settings panel. |
| Q23 | Save a copy | High / S–M | Write a separate project without changing the active project identity. |
| Q24 | Revert to saved | High / M | Restore an explicit known state with clear handling of unsaved work. |
| Q25 | Renameable project notes | Medium / S–M | A small notes field for mappings, external setup and performance intentions. |
| Q26 | Start blank / reopen last project preference | High / M | Give large-media users predictable startup; investigate #280 before choosing a default. [^29] |
| Q27 | Show asset size and remove unused assets | High / M | Make embedded project size understandable, with clear dependency checks. |
| Q28 | Missing-font notice and replacement choice | High / M | Avoid silently changing the appearance of someone else’s text project. |

### Performance and clarity

| ID | Improvement | Confidence / effort | Concrete scope |
|---|---|---|---|
| Q29 | Name LFOs and modulation sources | High / S–M | “Kick pulse” communicates intent better than a source number alone. |
| Q30 | Visible MIDI input and learned-mapping feedback | High / M | Brief activity plus the relevant channel/CC where useful; avoid a scrolling MIDI log by default. |
| Q31 | Adjustable hover-preview delay or disable toggle | Medium / S | Keep live effect previews available without surprising experienced users. |
| Q32 | Command search | Medium / M | Find existing actions by name; extra menu entries are not required. |
| Q33 | Keyboard focus and shortcut consistency | High / M | Audit text entry, effect navigation and host key forwarding as one focused pass. |
| Q34 | Current output summary | High / M | Show which audio, texture, preview and recording destinations are active. |
| Q35 | Click a source to see format/playback information | High / S–M | Dimensions, duration, frame rate and embedded/external status, as applicable. |
| Q36 | Per-object centre, fit, lock and solo | High / S–M after scenes | Essential contextual scene actions; not independent tiny features before M01. |
| Q37 | Per-object replace and duplicate with offset | High / S–M after scenes | Make basic layout fast and predictable. |
| Q38 | Unsaved marker for meaningful changes | High / M | Include effect and modulation edits, coordinated with M15 rather than a cosmetic label. |
| Q39 | Mono-input visualisation recipes | Medium / S–M | Curated presets using existing stereo/delay facilities; no new “mono support” claim. |
| Q40 | Readable disabled-control explanations | High / S–M | Explain host, source or recording constraints beside the affected control. |

Most basic convenience and protection features should benefit free users too. Premium should be appealing because it expands creative capability and saves substantial production work, not because ordinary editing is deliberately awkward.

## Premium and revenue opportunities

### Sell specific outcomes

The public site currently presents osci-render premium at $40 and highlights video, effects, recording, simulation and controls. The price is an observed listing, not a recommendation to change it or a guaranteed checkout price. Its visible feature summary does not explain every capability in current development. [^36]

Four concrete demonstrations would communicate the next tier better than a longer feature list:

1. **Build a personal visual identity:** combine a logo, title and animated object, then control it with four macros.
2. **Make a track respond:** load a project, select audio, set sensitivity and export a recognisable result.
3. **Perform a sound-image:** play a genuinely useful synth patch, move one control, and hear/see the change together.
4. **Keep the experiment:** capture the good moment and immediately reuse it in the DAW.

These are proposed positioning tests. No private conversion, refund, revenue or audience data was available, so expected financial impact cannot be quantified responsibly.

### Include a substantial starting library; sell optional depth

Develop a small number of excellent collections with editable projects, useful macros, source assets and clear rights for the included content. Strong themes include reactive typography, geometric identity systems, hardware-friendly musical patches and transparent overlay elements. Quality and variation matter more than a large preset count.

Test a creator pack through the existing purchase/distribution channel before building marketplace infrastructure. VS’s Battles expansion is listed at €19.99, while Synesthesia offers a scene marketplace; these are category references rather than a pricing formula for osci-render. [^24][^5]

A full marketplace adds curation, support, versioning, discovery and contributor economics. It is only justified once people are repeatedly sharing and buying compatible projects. An offline-first curated library and downloadable packs can test the central hypothesis much sooner.

### Let prospective buyers understand premium

Offer short, accurate previews of premium projects in the free app, and consider an explicit audition mode that leaves the person’s current work recoverable. A trial model should be evaluated separately from free-tier changes. Synesthesia’s watermarked trial is one possible precedent, not a reason to adopt that restriction here. [^37]

Measure whether people can explain what premium would let them create after seeing the demo. Product distinction also matters: osci-render makes sound-images; sosci visualises incoming sound. Shared rendering features should be described consistently across both.

### Treat reliability as a commercial feature

A failed export, lost edit or unrecognisable loaded project can outweigh several impressive demos. Ship recovery, clear source status and dependable recording alongside larger creative changes. Keep the installed application usable offline as the current licensing design intends; recurring content is a more natural experiment than changing the product into a mandatory subscription. [L1]

## Suggested sequence

| Stage | Work | Decision gate |
|---|---|---|
| Immediate | Q01, Q05–Q07, Q12, Q17, Q19 and unsaved/recovery design | Confirm the actual UI gaps in a short manual walkthrough; avoid duplicating local work. |
| First product package | M02 snapshots, M03 macros, a focused first version of M04 | Can an unfamiliar user perform and customise several excellent projects? |
| Scene feasibility | Mixed source types, direct 3D placement, camera integration and project-wide exposure bindings | Does composition preserve the intended output and existing playback/voice semantics? |
| Scene release | Agreed M01 overlay, all current source types, object transforms, per-scene cameras and shared effects/playback | Can artists assemble a useful composition while the normal workflow remains familiar? |
| Reactive package | M05 then M06 as demand requires; focused M08 prototypes | Are results predictable across ordinary music and source footage? |
| Musical differentiation | M10, M12 and expressive-source additions from M21 | Do musicians keep and use the resulting sounds in actual tracks? |
| Later expansion | M09 broader morphs, M16, M17, M19/M23, creator packs | Invest according to repeated workflow demand and successful earlier prototypes. |

This sequence describes the wider opportunity roadmap, not dependencies of the agreed first scene release. In particular, snapshots, macros, styling and per-object effects can be developed separately. The exact order should change with findings. In particular, M08 or M10 may beat scenes to release if a prototype produces much stronger usable results for less work. The recommendation is to validate the hardest scene assumptions early while delivering independent improvements.

## Features to defer

- **Full DAW arrangement, piano roll, clip sequencer and timeline automation:** outside the product boundary.
- **Laser output:** outside this research scope because it is already being investigated separately.
- **A general node editor:** wait until concrete reusable spatial behaviours make its benefit clear.
- **Full vector illustration, projection mapping or a general VJ compositor:** large neighbouring products; lightweight authoring and interoperability have stronger scope discipline.
- **A complete measurement/analyser suite for sosci:** some optional inspection may help, but the main shared opportunity is creative output.
- **AI text-to-scene as a headline:** the current bottlenecks are controllability, composition and useful starting material. A future helper should generate editable, inspectable projects and be judged against good presets.
- **Automatic song storyboards, stem separation and broad cloud collaboration:** high complexity with weak osci-render-specific demand in the evidence reviewed.
- **Mobile ports, AAX and broad format expansion:** potential audiences exist, but each brings platform/support costs. Validate demand before assigning roadmap priority.
- **More global distortions solely to grow the effect count:** prefer new control dimensions, source-local processing, or effects with a clearly different result.

## Evidence limits

This is a feature and workflow assessment, not a market-size or revenue model. Competitor documentation establishes functionality, not customer satisfaction or sales. Public issues and community posts are small, self-selecting samples; old limitations are dated explicitly. Absence in searched code is a reason to verify a proposed gap, not proof that no other branch implements it.

The baseline reflects source and release materials available on 8 September 2026. The checkout contains active uncommitted work. No runtime verification or engineering benchmark was performed for the proposals, and the effort bands should be revised after scoped prototypes.

## Sources

All web sources were consulted on 8 September 2026. Undated product pages describe the version advertised at access time; historical posts are dated where material. Numbered footnotes in the text correspond to this source inventory.

1. James H Ball, [osci-render release archive](https://github.com/jameshball/osci-render/releases), published tags through v2.8.9.18; full inventory retrieved through GitHub API.
2. James H Ball, [v1.33.2 historical changelog](https://github.com/jameshball/osci-render/blob/v1.33.2/src/main/resources/CHANGELOG.md), historical v1 features.
3. Hansi Raber / Oscilloscope Music, [OsciStudio](https://www.oscilloscopemusic.com/software/oscistudio/), product features.
4. Imaginando, [VS visual synthesizer](https://www.imaginando.pt/products/vs-visual-synthesizer), product features.
5. Gravity Current, [Synesthesia features](https://www.getsynesthesia.com/features), reactivity, library and marketplace.
6. Magic Music Visuals, [Features](https://magicmusicvisuals.com/features), composition, multiple inputs and preview.
7. Image-Line, [ZGameEditor Visualizer manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/ZGameEditor%20Visualizer.htm), layers, templates, sources and exports.
8. Resolume, [Dashboard](https://resolume.com/support/en/dashboard), multi-parameter performance controls.
9. Resolume, [Screens](https://resolume.com/support/en/screens), texture and NDI outputs.
10. Matt Tytel, [Vital](https://vital.audio/), visual modulation and wavetable synthesis.
11. Arturia, [Pigments](https://www.arturia.com/products/software-instruments/pigments/overview), modulation and macros.
12. FX23, [PsyScope Pro](https://fx23.net/PSYSCOPE-PRO/), and Blue Cat Audio, [Oscilloscope Multi](https://www.bluecataudio.com/Products/Product_OscilloscopeMulti/), analytical positioning.
13. Oscilloscope Music forum, [Design Techniques and Setup](https://forum.oscilloscopemusic.com/t/design-techniques-and-setup/176), August 2020; developer explanation of multi-object controls.
14. osci-render issue [#293: effect configuration recall](https://github.com/jameshball/osci-render/issues/293), user workflow and request.
15. osci-render issue [#156: multiple objects and morphing](https://github.com/jameshball/osci-render/issues/156).
16. osci-render issue [#286: choose recording destination first](https://github.com/jameshball/osci-render/issues/286).
17. osci-render issue [#154: unsaved changes warning](https://github.com/jameshball/osci-render/issues/154), historical request; checked against current source handling.
18. Oscilloscope Music forum, [Different FX for different objects?](https://forum.oscilloscopemusic.com/t/different-fx-for-different-objects/268), June–July 2021; user request and developer response.
19. Oscilloscope Music forum, [Saving Animations separately](https://forum.oscilloscopemusic.com/t/saving-animations-separately/677), August 2025–February 2026.
20. r/oscilloscopemusic, [Help me!](https://www.reddit.com/r/oscilloscopemusic/comments/1rchqjz/help_me/), February 2026; anecdotal hardware setup difficulty, not an independently verified diagnosis.
21. r/oscilloscopemusic, [Got an oscilloscope](https://www.reddit.com/r/oscilloscopemusic/comments/1ptqts5/got_an_oscilloscope/), December 2025; beginner workflow and sound/visual tradeoffs.
22. r/oscilloscopemusic, [Help with making loop seamless](https://www.reddit.com/r/oscilloscopemusic/comments/zpgksi), December 2022; sampler-loop workflow.
23. r/oscilloscopemusic, [Where can I find Lua scripts?](https://www.reddit.com/r/oscilloscopemusic/comments/1ldbfwa), June 2025; content discovery.
24. Imaginando, [VS Battles expansion](https://www.imaginando.pt/products/vs-visual-synthesizer/expansions/battles), observed pack offer.
25. Peter Selinger, [Potrace](https://potrace.sourceforge.net/) and [algorithm paper](https://potrace.sourceforge.net/potrace.pdf), bitmap contour tracing.
26. Noah Veltman, [Flubber](https://github.com/veltman/flubber), shape interpolation and documented limitations.
27. DJLevel3, [WavShaper](https://github.com/DJLevel3/WavShaper), two-dimensional waveshaping implementation and explanation.
28. osci-render issue [#195: wave shaping](https://github.com/jameshball/osci-render/issues/195).
29. osci-render issue [#280: heavy files at startup](https://github.com/jameshball/osci-render/issues/280).
30. osci-render issue [#202: Lua suggestions](https://github.com/jameshball/osci-render/issues/202), mixed completed and outstanding requests.
31. Ableton, [Link documentation](https://ableton.github.io/link/), tempo, beat and phase synchronisation.
32. MIDI Association, [Six New Profile Specifications Adopted](https://midi.org/6-new-profile-specifications-adopted), per-note expression discussion.
33. osci-render issue [#268: CLAP](https://github.com/jameshball/osci-render/issues/268), and free-audio, [CLAP API](https://github.com/free-audio/clap), format capabilities.
34. r/oscilloscopemusic, [Oscilloscope to Vector Graphic](https://www.reddit.com/r/oscilloscopemusic/comments/19d624e), January 2024; vector export workflow.
35. Khronos Group, [glTF](https://www.khronos.org/gltf/), scene and animation format.
36. James H Ball, [osci-render product site](https://osci-render.com/), public product distinction, premium features and observed $40 listing.
37. Gravity Current, [Synesthesia pricing](https://getsynesthesia.com/pricing), trial and edition structure.
38. osci-render issue [#289: automatable recording](https://github.com/jameshball/osci-render/issues/289), closed request; completion was not assumed from status.

### Local baseline references

These references describe the local development tree, including uncommitted work, and are not claims about availability in the public release.

- **L1:** [Local release notes](../../RELEASE_NOTES_2.9.5.1.md), [format registry](../../Source/parser/FileFormatRegistry.h), [processor setup](../../Source/PluginProcessor.cpp), and [visualiser settings](../../Source/visualiser/VisualiserSettings.h).
- **L2:** [Modulation registry](../../Source/audio/modulation/ModulationTypes.h), [sidechain implementation](../../Source/audio/modulation/SidechainState.h), and the locally added [wheel parameters](../../Source/audio/modulation/WheelParameters.h).
- **L3:** [Lua documentation](../../Source/components/lua/LuaDocumentationComponent.cpp), [Lua controls](../../Source/components/panels/LuaComponent.cpp), and [LFO component](../../Source/components/modulation/LfoComponent.h).
- **L4:** [Effect picker](../../Source/components/effects/EffectTypeGridComponent.h), [effect list and randomisation](../../Source/components/effects/EffectsListComponent.h), and [parameter menu](../../modules/osci_gui/components/osci_ParameterContextMenu.h).
- **L5:** [Point representation](../../modules/osci_render_core/shape/osci_Point.h), [voice rendering](../../Source/audio/synth/ShapeVoice.cpp), and [visualiser mode selection](../../Source/visualiser/VisualiserComponent.cpp).
- **L6:** [Image parser](../../Source/parser/img/ImageParser.cpp).
- **L7:** [OBJ path construction](../../modules/osci_file_import/obj/osci_WorldObject.cpp).
- **L8:** [File controller](../../Source/FileController.h), [common processor](../../Source/CommonPluginProcessor.cpp), and [project operations](../../Source/CommonPluginEditor.cpp).
- **L9:** [Recording controller](../../Source/visualiser/VisualiserRecordingController.cpp) and [recording settings](../../Source/visualiser/RecordingSettings.h).

## Source notes

[^1]: James H Ball, [osci-render release archive](https://github.com/jameshball/osci-render/releases), published tags through v2.8.9.18; full inventory retrieved through GitHub API.
[^2]: James H Ball, [v1.33.2 historical changelog](https://github.com/jameshball/osci-render/blob/v1.33.2/src/main/resources/CHANGELOG.md), historical v1 features.
[^3]: Hansi Raber / Oscilloscope Music, [OsciStudio](https://www.oscilloscopemusic.com/software/oscistudio/), product features.
[^4]: Imaginando, [VS visual synthesizer](https://www.imaginando.pt/products/vs-visual-synthesizer), product features.
[^5]: Gravity Current, [Synesthesia features](https://www.getsynesthesia.com/features), reactivity, library and marketplace.
[^6]: Magic Music Visuals, [Features](https://magicmusicvisuals.com/features), composition, multiple inputs and preview.
[^7]: Image-Line, [ZGameEditor Visualizer manual](https://www.image-line.com/fl-studio-learning/fl-studio-online-manual/html/plugins/ZGameEditor%20Visualizer.htm), layers, templates, sources and exports.
[^8]: Resolume, [Dashboard](https://resolume.com/support/en/dashboard), multi-parameter performance controls.
[^9]: Resolume, [Screens](https://resolume.com/support/en/screens), texture and NDI outputs.
[^10]: Matt Tytel, [Vital](https://vital.audio/), visual modulation and wavetable synthesis.
[^11]: Arturia, [Pigments](https://www.arturia.com/products/software-instruments/pigments/overview), modulation and macros.
[^12]: FX23, [PsyScope Pro](https://fx23.net/PSYSCOPE-PRO/), and Blue Cat Audio, [Oscilloscope Multi](https://www.bluecataudio.com/Products/Product_OscilloscopeMulti/), analytical positioning.
[^13]: Oscilloscope Music forum, [Design Techniques and Setup](https://forum.oscilloscopemusic.com/t/design-techniques-and-setup/176), August 2020; developer explanation of multi-object controls.
[^14]: osci-render issue [#293: effect configuration recall](https://github.com/jameshball/osci-render/issues/293), user workflow and request.
[^15]: osci-render issue [#156: multiple objects and morphing](https://github.com/jameshball/osci-render/issues/156).
[^16]: osci-render issue [#286: choose recording destination first](https://github.com/jameshball/osci-render/issues/286).
[^17]: osci-render issue [#154: unsaved changes warning](https://github.com/jameshball/osci-render/issues/154), historical request; checked against current source handling.
[^18]: Oscilloscope Music forum, [Different FX for different objects?](https://forum.oscilloscopemusic.com/t/different-fx-for-different-objects/268), June–July 2021; user request and developer response.
[^19]: Oscilloscope Music forum, [Saving Animations separately](https://forum.oscilloscopemusic.com/t/saving-animations-separately/677), August 2025–February 2026.
[^20]: r/oscilloscopemusic, [Help me!](https://www.reddit.com/r/oscilloscopemusic/comments/1rchqjz/help_me/), February 2026; anecdotal hardware setup difficulty, not an independently verified diagnosis.
[^21]: r/oscilloscopemusic, [Got an oscilloscope](https://www.reddit.com/r/oscilloscopemusic/comments/1ptqts5/got_an_oscilloscope/), December 2025; beginner workflow and sound/visual tradeoffs.
[^22]: r/oscilloscopemusic, [Help with making loop seamless](https://www.reddit.com/r/oscilloscopemusic/comments/zpgksi), December 2022; sampler-loop workflow.
[^23]: r/oscilloscopemusic, [Where can I find Lua scripts?](https://www.reddit.com/r/oscilloscopemusic/comments/1ldbfwa), June 2025; content discovery.
[^24]: Imaginando, [VS Battles expansion](https://www.imaginando.pt/products/vs-visual-synthesizer/expansions/battles), observed pack offer.
[^25]: Peter Selinger, [Potrace](https://potrace.sourceforge.net/) and [algorithm paper](https://potrace.sourceforge.net/potrace.pdf), bitmap contour tracing.
[^26]: Noah Veltman, [Flubber](https://github.com/veltman/flubber), shape interpolation and documented limitations.
[^27]: DJLevel3, [WavShaper](https://github.com/DJLevel3/WavShaper), two-dimensional waveshaping implementation and explanation.
[^28]: osci-render issue [#195: wave shaping](https://github.com/jameshball/osci-render/issues/195).
[^29]: osci-render issue [#280: heavy files at startup](https://github.com/jameshball/osci-render/issues/280).
[^30]: osci-render issue [#202: Lua suggestions](https://github.com/jameshball/osci-render/issues/202), mixed completed and outstanding requests.
[^31]: Ableton, [Link documentation](https://ableton.github.io/link/), tempo, beat and phase synchronisation.
[^32]: MIDI Association, [Six New Profile Specifications Adopted](https://midi.org/6-new-profile-specifications-adopted), per-note expression discussion.
[^33]: osci-render issue [#268: CLAP](https://github.com/jameshball/osci-render/issues/268), and free-audio, [CLAP API](https://github.com/free-audio/clap), format capabilities.
[^34]: r/oscilloscopemusic, [Oscilloscope to Vector Graphic](https://www.reddit.com/r/oscilloscopemusic/comments/19d624e), January 2024; vector export workflow.
[^35]: Khronos Group, [glTF](https://www.khronos.org/gltf/), scene and animation format.
[^36]: James H Ball, [osci-render product site](https://osci-render.com/), public product distinction, premium features and observed $40 listing.
[^37]: Gravity Current, [Synesthesia pricing](https://getsynesthesia.com/pricing), trial and edition structure.
[^38]: osci-render issue [#289: automatable recording](https://github.com/jameshball/osci-render/issues/289), closed request; completion was not assumed from status.
