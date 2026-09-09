# Release Notes: osci-render 2.9.5.1 / sosci 1.4.5.1
## Major New Features
- Transparent visualiser backgrounds and transparent video recording on macOS and Windows
- New visualiser popouts with click-through, always-on-top, full-screen, and remembered window settings
- MIDI Program Change visual selection, new file actions, and additional MIDI and output routing controls in osci-render
- Lottie animation support and expanded animation playback controls in osci-render premium
- Rectangular and custom visualiser canvases for recording, offline rendering, and texture sharing
- New project, recording, and recent-file workflows across osci-render and sosci
- New installer, account, automatic update, and in-app feedback experiences
- Native Linux installation with standalone and VST3 location selection

## osci-render (premium only)
### Lottie Animations
- New Lottie animated vector support for `.json`, `.lottie`, and `.lot` files
- Lottie animations can be opened through the file browser or drag and drop, and use the existing animation timeline and playback controls
- 12 bundled Lottie examples: Android Wave, Switch, Heart, Cat, Dice, Pin Jump, Lottie Logo 1, Lottie Logo 2, Hello, Spinning Squares, Wave, and Blinking Eye
### Audio
- New Internal Sample Rate menu for the standalone app, with 1x, 2x, 4x, and 8x processing rates
- Internal sample-rate changes are applied across the synth, effects, Lua scripts, MIDI timing, and output, while the audio device continues running at its configured rate
### Video Import
- Fixed the app becoming unresponsive when a video was opened without FFmpeg installed
- Video imports now remain pending while the FFmpeg download prompt is open and continue automatically after FFmpeg is installed
### Modulation
- LFO Sync mode remains available in plugin hosts when MIDI input is disabled, with timing held correctly while the host transport is paused
- Fixed envelope tabs displaying stale graph after switching between envelopes

## osci-render (both versions)
### MIDI Program Change
- MIDI Program Change messages can now select visuals in the current project
- Program Change 0 selects the first visual, Program Change 1 selects the second, continuing through the first 100 visuals
- Program Change input can be disabled, received on all MIDI channels (omni), or limited to a specific channel from 1 to 16
- MIDI Program Change settings are available by clicking on the current file name
- The standalone app remembers the selected MIDI channel globally, while plugin instances save it with the host project
### File Controls
- The current file name is now interactive, with hover feedback and a context menu when clicked
  - New file actions: Edit file for text and Lua files, Rename file, Duplicate file, Export file, and Remove file
- File Select automation and out-of-range Program Change selections resolve to the nearest available visual instead of leaving an invalid selection
- Existing projects retain their saved file order, embedded content, and current selection
### MIDI and Output Routing
- New MIDI input channel selection, Panic, and Kill Voices Immediately controls
- New Swap X/Y, Invert X, and Invert Y output controls
### Animation
- Supported animated files now play at their embedded frame rate, including GIF and GPLA in both versions, plus video and Lottie in premium
- Animation Rate has been replaced by a modulatable Animation Speed multiplier
- Existing projects using Animation Rate are converted to an equivalent Animation Speed value without changing their playback speed
- Negative Animation Speed values play animations in reverse
- GPLA files now use their stored frame rate
### Fixes
- Fixed the Lua editor briefly stalling when reporting the first syntax error
- Fixed slowdowns when moving between effect previews

## sosci + osci-render (premium only)
### Transparent Visualiser
- New Transparent Background option which works with all screen overlays
- Transparent visualiser window on macOS and Windows, with a checkerboard background on Linux
- New window controls for showing or hiding the frame, always on top, click-through, full screen, and closing the window
- Pausing restores the popout controls, and the popout can also be closed from the main visualiser
### Transparent Video Recording
- New ProRes 4444 recording option with transparency support
- Enabling Transparent Background automatically selects ProRes 4444 and saves the recording as a `.mov` file
- ProRes 422 HQ remains available for recordings without transparency
### Texture Sharing
- Texture inputs can now be selected directly from the Video menu, with source application names and dimensions shown when available
- Texture output now follows the current visualiser frame and uses the selected rectangular or custom canvas size
- Clearer status and error overlays are shown when a texture source disappears or texture input or output cannot start
- Closing an editor, disconnecting a texture source, or switching back to project files now clears texture input cleanly and restores the selected visual
### Offline Audio-to-Video Rendering
- Improved offline rendering diagnostics for input files, rendering progress, OpenGL, and FFmpeg failures
- Improved cancellation and missing-frame handling
### Visualiser and Recording
- New rectangular visualiser canvas support for preview, recording, offline rendering, and texture output
- New 1024 x 1024, 1920 x 1080, and 1080 x 1920 canvas presets
- Custom canvas width and height from 128 to 4096 pixels
- Realistic CRT screen overlays are disabled for non-square canvases where they would display incorrectly

## osci-render + sosci (all versions)
### Licensing and Automatic Updates
- New optional licensing system with an Account window for activating, viewing, copying, revealing, and removing a license
- A license key is only required to download a premium installer or premium update
- Cached licenses refresh automatically when possible, while an expired or offline license does not disable the installed application
- Free osci-render users can activate a premium license and download the premium version from the Account window
- New update notifications with Update, Install, Later, and close actions when relevant
- A compact dismissible notification confirms when the updated version starts successfully after installing
- Failed or incomplete installations can be retried from the update notification
- Stable and beta update channels are supported, with beta updates enabled or disabled by clicking the version status five times in the About menu
- A visible Beta Updates button appears in the top bar while the beta channel is active
### Feedback
- New Send Feedback option in the application menu and About window
- Submit a bug report or feature request directly from the application
- Required contact email, title, and description fields are validated before submission
- The current application view can be attached automatically and previewed or removed before sending
- Add up to four PNG or JPEG screenshots by browsing or drag and drop, with thumbnail and full-size previews
- Optional report settings can include a privacy-filtered diagnostic log and a copy of the current project
- Reports include relevant application, version, operating system, host, display, renderer, audio, and release-channel information
### Audio
- Setting the output clipping threshold to its maximum now bypasses clipping instead of limiting the signal to 1.0
- The output meter only shows the clipping cap when clipping is active and the threshold has actually been reached
- Added AAC audio import support on macOS
### Visualiser and Workflow
- Visualiser Settings now adapt to different window sizes
- New trigger source and slope controls
- Supported projects and media files can now be opened from the command line or operating system
- Fixed visualiser texture rendering on Windows
### Interface
- More application messages now use consistent in-app overlays, including Account, FFmpeg, recording, texture-sharing, file-loading, and confirmation messages
- Error and confirmation overlays share consistent close controls and visual styling
- About now opens as an in-app overlay and includes the new Send Feedback and beta update controls
- Popup-menu selection ticks and spacing have been adjusted
- Visualiser Settings rows now match the styling used by effect controls
- Added standard keyboard shortcuts and clearer menu grouping
- Fixed shortcut labels and the Listen for Special Keys menu item
### Logging and Diagnostics
- Startup logs now include more useful application, wrapper, JUCE, operating system, hardware, and locale information

## osci-installer
- New dedicated installer application for osci-render and sosci
- Choose between free and premium osci-render, or install premium sosci
- Existing cached licenses are reused and refreshed automatically when possible
- The installer downloads and verifies the latest stable release for the current platform, then asks for confirmation before launching it
- Distributed as a signed and notarized DMG on macOS, an installer executable on Windows, and a standalone binary on Linux
- New ARM64 Linux builds for osci-render, sosci, and osci-installer
- Updated osci-render, sosci, and osci-installer application icons on macOS and Windows
- New native Linux installation flow with separate standalone and VST3 installation locations
- The installer and in-app updater now share the same installation flow
