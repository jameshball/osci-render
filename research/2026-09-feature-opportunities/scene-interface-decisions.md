# Scene editor implementation decisions

The scene editor occupies the existing right-hand panel, selected through Effects / Scene / Editor / Examples tabs. It uses the application's typography, buttons and accent colour. The visualiser stays visible on the left throughout scene editing. Selecting an object does not change the shared effects chain.

The separate scene overlay, cube shortcut in the filename bar, Add object menu and custom source-editing dialog have been removed. The existing plus button and Examples browser add to the current scene while in scene-editing context, with an explicit Add to scene heading. Effects restores normal file opening. All source editing opens the shared Editor tab. The object list supplies a pencil for editable sources, an Edit source context-menu action, and double-click access. Returning from Examples preserves object selection.

The compact object list supplies reliable selection for overlapping sources. Most space goes to the scene preview. A single inspector contains position, rotation and scale; cameras use target, rotation and view size. Numeric fields support dragging, fine adjustment with Shift and double-click typing. Move, Rotate and Scale are visible tools with W/E/R shortcuts. Camera orbit is a background drag, pan is Shift-drag, and scrolling changes view size. Front/Side/Top and Frame provide ways to recover a useful view without knowing shortcuts.

This follows the discoverable axis/navigation controls in [Blender's navigation interface](https://docs.blender.org/manual/en/latest/editors/3dview/navigate/introduction.html), while keeping the much smaller task surface of osci-render. [Spline's navigation shortcuts](https://docs.spline.design/basics/keyboard-shortcuts) informed support for both pointer drags and modified navigation gestures. A separate inspection camera, hierarchy editor, lighting controls and material inspectors are unnecessary for this release.

The preview uses the project-wide Perspective strength and FOV through the same PerspectiveProjector as the output. Other creative effects remain outside the editor preview. Orbiting changes the actual scene camera. Wire geometry and source traces keep the editor legible while using the application's familiar oscilloscope colour, without exposing editing guides in audio or recorded output.

Visual iteration caught and removed overflow in the original fixed-height overlay and replaced conventional slider bars with compact numeric fields. The scene overlay now fits the current plugin window instead of requiring scrolling to reach scale controls. Native/Jucewright screenshots are kept in the local test artifact directory, `/tmp/osci-scene-ui`.

Eight fixed project-wide entity slots keep the host parameter inventory bounded. Slot assignment is explicit; adding objects consumes no slots. Each slot remains attached to its saved identity across scene switches. Duplicating a scene or object copies its transform without inheriting its automation assignment.

## Validation

The macOS Debug standalone build passed. Pluginval passed at strictness 5, and the Scene unit-test category passed. The integration harness linked against the app checks rendered transforms, project-wide host bindings, independent cameras, complete scene duplication, project save/reload, shared live geometry, different animation durations, and finite non-silent output through the synth/effects path.

The focused Jucewright suite passed 68 actions covering direct move/rotate/scale drags, text editing, object and camera exposure, Lua and live-source placeholders, menu dismissal, fourteen objects in a scene, and normal file opening into another scene. Screenshots were reviewed at 900 × 650 and 650 × 500. The latter uses a scaled compact layout. The existing broad browser smoke run had a nonblank-visualiser screenshot failure while the OpenGL view was unavailable; it was not a wholly passing run. The scene preview and engine output were verified separately. External Blender/texture applications were not connected during this validation; shared live geometry was exercised through the engine harness.

Reproduce current panel checks with `python3 scripts/test_scene_panel_with_jucewright.py --session scene-editor --artifact-dir /tmp/osci-scene-panel`. The original overlay test was superseded by this workflow. The runner sanitizes the test audio profile before launch. `python3 scripts/run_scene_integration.py /tmp/scene-build.log` links the engine harness using the compiler configuration from the standalone build log.

## Native screenshot follow-up

The black visualiser screenshots were caused by the macOS presentation path: the completed frame is an IOSurface in a native view beneath JUCE, while the JUCE component paints a transparent hole. Jucewright now exposes a generic component-screenshot scope, and the macOS presenter paints its completed surface during that scope only. No render semaphore logic changed. The dedicated screenshot regression passes for component and automatic capture, scene-editor close/reopen presentation, and resizing. Whole-window and pop-out screenshots were also visually checked with rendered output. Artifacts: `/tmp/osci-screenshot-fix`. This resolves the required nonblank-capture failure; the unrelated optional steps of the broad smoke suite have not been rerun.

Run `python3 scripts/test_visualiser_screenshots_with_jucewright.py --session scene-editor --artifact-dir /tmp/osci-screenshot-fix` after building to reproduce the screenshot checks.

## Captured camera navigation

Navigate in the scene toolbar explicitly enters mouse-captured camera control. Mouse movement looks around without holding a button; WASD and arrows move the view, Q/E lower/raise it, and Shift increases speed. Forward/backward now moves the camera in 3D; sideways and vertical movement follow its orientation. Mouse look preserves the projection eye position rather than orbiting the scene target. Escape or focus loss releases the mouse, restores its screen position, and restores the previous object selection. Normal object shortcuts and drag-to-orbit remain available outside navigation. One navigation session is one undo transaction, and exposed camera parameters receive the existing host gestures.

The Debug standalone build and `scripts/test_scene_navigation_with_jucewright.py` passed. Coverage verifies mouse movement without a drag, keyboard movement, unchanged object transforms, Escape, repeated entry, and focus-change exit. The running app was relaunched after building and left outside mouse capture. Screenshot: `/tmp/osci-navigation/navigation-mode.png`.

## Perspective and movement refinement

Camera navigation polls held keys at 60 Hz, with time-based acceleration and deceleration. Key-press/repeat callbacks no longer apply movement increments. Direction is normalized so diagonals and simultaneous WASD/arrow aliases do not move faster. Motion integration is tested at 30, 60 and 120 updates per second with equivalent travel. The native held-key injection attempt was unavailable because the test process lacks macOS event-posting permission; the math tests and semantic UI tests passed, including confirming that repeat callbacks alone cause no movement. Mouse hover without dragging changes pitch/yaw, preserves object transforms, and exits correctly on Escape/focus change.

The updated Debug standalone build and Scene unit tests passed. Preview screenshot: `/tmp/osci-perspective-nav/camera-navigation.png`. Uniform object scaling remains R, then drag the centre handle.


## Integrated-panel validation (9 September)

Debug standalone builds and the integrated-panel Jucewright checks passed, including adding an example to a scene through the shared browser, root file drops, transforms, embedded source editing, DAW exposure, live Blender object insertion, and returning to ordinary file opening from Effects. Visual review covered 1100 × 770 and 900 × 650 with the live output and editing preview visible together. Camera and screenshot regression scripts now target the Scene tab. External live-source applications and native file chooser interaction are not covered by this focused run.

The integrated panel retains captured camera navigation. Direct pointer dispatch verifies rotation and eye movement, and Escape/focus-change release is checked. Native hover dispatch did not reach the preview in this automation environment, so mouse-only hover validation remains inconclusive; this is explicitly reported by the navigation script rather than treated as a successful hover check.

Final focused runs: 39 integrated-panel actions, 25 screenshot-regression actions and 16 camera-navigation actions completed. Inline source edits were reopened and verified, and deleting the final scene removed its editor. Reviewed screenshots are saved in `screenshots/2026-09-09/`. The native hover limitation above still applies.


## Shared tabs and Editor workspace

The right-hand workspace uses `osci::TabBar`, a reusable JUCE tab strip in osci_gui. [Adobe Spectrum's horizontal tabs](https://spectrum.adobe.com/page/tabs/) informed the text-led navigation and narrow active indicator. The header uses the same dark rounded rectangle as the filename controls, with the application's bundled Fira Sans typeface. The full-width divider and JUCE's inherited tab-area line are removed.

Effects, Scene, Editor and Examples share this navigation. Files, scene objects and Lua effects open the Editor tab, with the existing source component showing the document name. The separate editor column and the inline scene editor have been removed. Text retains its font selector; Lua retains sliders, console, errors, help and reset. Explicit Lua-effect editing keeps its target until another source is chosen. Returning to Scene preserves the object selection and camera; returning to the same source retains its editor state.

Scene has three rounded panels using the standard group surfaces: an object list on the left, the interactive preview in the centre, and a compact inspector on the right. Position, rotation and scale use three axis fields per section. The toolbar is a separate 30 px rounded header, aligned with Objects and the selected object name. It stays on one row in compact layouts; the drawing area beneath is a separate, uniformly dark panel. Numeric fields retain dragging, Shift for fine adjustment and double-click entry. The drawing preview keeps a dark surface, while the surrounding controls use the application's standard grey bodies and dark headers.

In plugin builds, the automation button reads “Automation: slot N” when assigned; standalone hides DAW assignment controls. Its tooltip explains that one project-wide slot exposes the selected object's or camera's transform controls and stays bound across scene changes. Creating or selecting an object does not consume a slot.

## Editor workspace validation

The Debug standalone build passed. Focused Jucewright runs completed 41 scene-panel actions and 28 shared-Editor actions: object transforms, pencil and context-menu editing, source persistence, ordinary Lua files, Lua sliders/reset, effect-source routing, console interaction, and closing the source while Editor is open. Screenshots were reviewed at 1100 × 770 and 900 × 650 and are in `screenshots/2026-09-09-editor-workspace/`.

The final camera run completed 15 actions. Native hover reached the captured preview in this run, so mouse-look was verified without the direct-drag fallback needed in earlier runs. Escape and focus-change release also passed. Semantic key repeats did not introduce movement; these UI checks do not simulate sustained native keyboard state. The existing time-based motion tests cover that integration separately.

The Mac speakers remained muted. This pass changes UI and editor routing; it does not alter the visualiser render-semaphore synchronization or add audio parameters.

## Surface and standalone refinements

Compared three Scene treatments in the running application: standard grey panels with a dark preview, darker neutral panels, and grey throughout including the preview. Also compared the standard grey source-editor background against medium and dark neutral alternatives. Comparison captures are in `screenshots/2026-09-09-colour-study/`.

Selected standard group-body grey and dark headers for the Scene side panels, matching Effects and the modulation panels. The drawing preview stays dark to separate geometry from controls. The source editor uses a dark neutral derived from the existing palette. Removed the outer Editor group and duplicate title; its source/console and slider sections occupy the workspace directly. A duplicated object-row paint pass was also removed, fixing excessively strong selection colouring and doubled text.

Standalone hides the automation assignment button and context-menu entry. Plugin builds retain these controls and existing assignments. This changes visibility only, with no change to scene parameters or state.

The final surface pass built successfully and passed 42 scene UI actions plus 28 Editor actions. Checks cover the missing outer container, hidden standalone assignment button/menu, camera context behaviour, source editing, and normal/compact layouts. Final reviewed screenshots are in `screenshots/2026-09-09-surfaces/`.

## Shared headers and panel spacing

[Design guidelines](../../DESIGN_GUIDELINES.md) records the deliberately brief rules: 3 px between adjacent panels, larger gaps only for visible dividers/resizers, and aligned 30 px headers.

`osci::PanelHeader` supplies the header surface, dimensions and content inset. Scene uses it for all three columns; the workspace tabs and filename bar share its background painter. The preview toolbar is disconnected from the uniformly dark drawing/guide area below. Scene columns and header/body boundaries use 3 px gaps. Editor font/source and source/console spacing, Effects auxiliary sections, and Examples category spacing follow the same rule. Real workspace and Lua resize handles keep their width; internal control padding remains distinct from panel spacing.

The Objects and selected-object headers are attached to their grey panel bodies, matching the application's group-component treatment. The preview header remains separate because it acts as a toolbar for the dark canvas below it.

The shared-header build passed, followed by 42 scene actions and 28 Editor actions. Layout assertions verify three aligned 30 px headers and 3 px column gaps, including the compact window. Visual review covered Effects, Scene, Editor and Examples. Reviewed captures are in `screenshots/2026-09-09-aligned-panels/`.
