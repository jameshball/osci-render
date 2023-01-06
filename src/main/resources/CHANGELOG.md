- 1.33.0
  - Overhaul how animations work in the interface for more precise and intuitive control over the animation speed
    - When the animation type changes, the range of the slider changes and changing the slider changes the frequency of the animation
    - The value of the slider when there is no animation (Static) is saved and restored when the animation is removed
    - Animation speeds vary from 0Hz to 100Hz
    - You should not notice any difference in how animation is enabled or controlled, but the animation speed slider now goes much faster
  - As before, to control the range of the animation, just change the range of the slider from the "Slider" menu
  - Add new animation type: "Sine"
  - Add new animation type: "Square"
  - Rename "Linear" animation to "Triangle"
  - Rename "Forward" animation to "Sawtooth"
  - Rename "Reverse" animation to "Reverse Sawtooth"
  - These new changes should be backwards compatible with projects in prior versions
  - If you notice any issues or differences in projects loaded from older versions, please let me know!


- 1.32.3
  - Only check for XTAudio devices if not on macOS as it is not available on macOS


- 1.32.2
  - Add message to project select screen if there is a new version available


- 1.32.1
  - Allow projects to be openable from the project select screen, even if they haven't been opened before


- 1.32.0
  - Added two new audio effects: Delay/Echo and Bulge
  - Delay effect can be used to create an echo of the audio signal
    - Controllable with both the echo length and the decay of the echo
  - Bulge effect makes the image bulge outwards
    - If you change the minimum slider value to something negative using the Slider settings, you can make the image bulge inwards


- 1.31.0
  - Support audio input from microphone/sound card as an audio source
    - Just click the microphone button next to where you choose a file to open
    - This will replace the current audio source with the microphone
    - You can choose which input device in the Audio settings, just like you would for an output device
    - Different sample rates are supported and will be resampled if they do not match the output device
    - Changing the input device also changes which input device is used for controlling effects with the microphone
  - Minor performance improvements
  - As always, please report any bugs you find on [GitHub](https://github.com/jameshball/osci-render/issues)!


- 1.30.3
  - Dramatically improved MIDI performance! Should now be pretty resilient to anything you throw at it
  - Renamed decay to release in MIDI settings
  - Version number is now saved with .osci project file


- 1.30.2
  - Allow text size to be specified when creating .txt files
    - Write "$text_size=" followed by a number to change the text size for that line
    - e.g. "$text_size=50" will make the text size 50 for that line
    - The default text size is 100
    - If for some reason you still want to write "$text_size=", you can escape it with a backslash. e.g. "\\$text_size=50"
    - This slightly changes the syntax for writing a backslash, as you now need to write "\\\\" instead of "\\"
  - Fix a bug where the animations would get stuck if the bounds of the slider/effect were changed while the animation was playing
  - The knobs/thumbs of sliders now change to green when the slider/effect is being animated


- 1.30.1
  - Changed Lua demo file to the mini osci-render demo that emulates a basic version of osci-render within osci-render
  - Done this as the old demo's audio was quite jarring and the new one shows off more of Lua


- 1.30.0
  - Updated osci-render to Java 19 (please report any new issues you think could be related to this!)
  - Added volume slider on right side of main interface
    - This replaces the volume scale slider under audio effects
    - The slider has a volume visualiser that shows the current volume in the left and right audio channels
    - It also shows the average volume
    - There is also another slider that points to the volume slider that controls the hard clipping point of the audio
      - The hard clipping point is the point at which the audio is clipped to the maximum volume
      - Any samples above this point are set to this point
      - By default, this is set to 1 meaning that any values less than -1 or greater than 1 are set to -1 or 1 respectively


- 1.29.0
  - Updated the project select interface and main interface to be resizable
  - This means you can make osci-render full screen now
  - The default size is the same as before, and should look almost identical


- 1.28.5
  - Fixed various bugs:
    - Fixed rotation setting isn't set correctly when opening a project [#128](https://github.com/jameshball/osci-render/issues/128)
    - Undo/redo carries over between different files in the text editor [#120](https://github.com/jameshball/osci-render/issues/120)
    - Fonts and font styles chosen for text file rendering are not saved to a project [#113](https://github.com/jameshball/osci-render/issues/113)


- 1.28.4
  - Fix calculation of line length
  - Use rotation for Ellipse shapes
  - Thank you [DJLevel3](https://github.com/DJLevel3) for the contributions!
  - Use setReuseAddr(true) for server socket


- 1.28.3
  - Fixed bug: Update animator minimum and maximum values whenever the slider min/max changes


- 1.28.2
  - Fix slider minimum and maximum value changes
  - Change size of .lua file settings panel


- 1.28.1
  - Remove the scrollability of the translation effect text boxes since they are on a scrollable pane making them confusing to use
  - Fixes [#111](https://github.com/jameshball/osci-render/issues/111)
  - Thank you [Kar-3kn](https://github.com/Kar-3kn) for this fix and for your first ever open-source contribution!


- 1.28.0
  - Add buttons next to 3D rotation effects to change the rotation in the selected axis to be fixed
    - These appear next to the 3 sliders for 3D object rotation, as well as the 3 sliders for the 3D perspective effect rotation
    - This means that it will never move in this axis when the rotation speed is non-zero
    - Allows you to rotate one axis, whilst specifying the exact rotation of the object in another axis
    - Thanks again to [@javierplano_videonix](https://www.instagram.com/javierplano_videonix/) for the suggestion!
  - Update all sliders (except frequency slider) to use the same (updated) design with the following:
    - Slider
    - Value spinner
    - MIDI button
    - Animation type dropdown
    - Mic button
      - This has been changed to a mic icon rather than a checkbox
    - This means that the 3D object sliders and Lua sliders can now all be animated and controlled with a microphone!
  - Various project-related bugfixes
    - Backwards compatibility with old project files should be better
    - Opening projects should be more consistent with how it would look if you opened the project right after starting osci-render
    - Project files that don't exist are removed from the list when you try and open them


- 1.27.7
  - Change audio selection and text font selection to be lists rather than dropdowns


- 1.27.6
  - Fix trace min effect being hidden at bottom of effects section


- 1.27.5
  - Add additional decimal place on effect spinners so that the values change correctly
    - Fixes [#80](https://github.com/jameshball/osci-render/issues/80)


- 1.27.4
  - Allow the 3D perspective effect to be live-programmed with Lua
  - By default, the behaviour is the same as before and the image is not modified
  - Variables `x`, `y`, and `z` are available to used to transform the 3D position of the image
  - `step` is also available as with regular live-coding
    - This lets you animate the effects you apply
  - Most interesting transformations include modifying the depth to be a function of `x` and `y`
    - Example: `return { x, y, z + 0.1 * math.sin(x * 10 + step / 100000) }`
    - This creates a wave effect for the image which can be visualised more clearly by rotating the image
  - Functions written are saved with the project


- 1.27.3
  - Fix [#102](https://github.com/jameshball/osci-render/issues/102) which caused errors when using trace min and small focal lengths at the same time
    - Thanks [Luiginotcool](https://github.com/Luiginotcool) for reporting this issue!
  - Reduce max log file size to 1MiB (was previously unlimited)


- 1.27.2
  - Add reset 2D rotation button to effects
  - Add reset 3D perspective rotation button to effects
  - Correctly show recent files under File menu when starting a new project
  - Minor changes to perspective effect
    - Added a new '3D Distance' effect that functions the same as the '3D Perspective' effect in the previous version
    - Changed the '3D Perspective' effect so that a value of 0 applies no change to the image
      - This makes it in-line with most other effects, where a value of 0 doesn't affect the image
      - Default value is 1.0 which has the depth effect fully applied
      - Values between 0.0 and 1.0 mix the effect to make a cool image!


- 1.27.1
  - Improve performance of project select screen by only starting main osci-render program after a project is opened/created
  - Fix bug where images could be drawn so slowly that the frame would never finish drawing
  - Add recent projects under File > Open Recent Project
  - Update recent projects when a project is opened


- 1.27.0
  - Add project select screen!
    - Includes this changelog
    - Adds toggle that is permanently saved to start osci-render muted
    - Adds button to easily open the folder where logs are saved
    - Shows last 20 previously opened projects
      - These can be clicked on to open a previous project
  - Allow any font that has been installed on your machine to be used with text files
    - Change font by selecting a font from the drop-down under View > Text File Font
    - Change font style (i.e. plain, bold, italic) by selecting a font style from the drop-down under View > Text File Font Style


- 1.26.4
  - Make GPU rendering toggleable to prevent crashes due to bad initialisation
  - Can be toggled under View > Render Using GPU


- 1.26.3
  - Fix potential bug with updating frequency spinner and slider from not-GUI thread


- 1.26.2
  - Add more error logging


- 1.26.1
  - Add error logging to the directory osci-render is run from `/logs/`


- 1.26.0
  - Reorganise the interface so that more sliders are considered audio effects
    - This means you can animate things like volume, rotation, and translation!
  - Add a precedence to audio effects so that they are consistently applied in the same order
    - Lower precedence means the effect will be applied earlier
    - Precedence of current effects from lowest to highest:
      - Vector cancelling
      - Bit crush
      - Vertical distort
      - Horizontal distort
      - Wobble
      - Rotate 2D
      - Translation
      - Depth 3D
      - Smoothing
    - This means that smoothing is always applied last
  - Add 3D audio effects
    - This treats the image being displayed as a 3D object and rotates it
    - This means you can now rotate 2D images, like SVGs and text files
    - COMING SOON: A way to control the depth or Z-coordinate of the image using a depth-map/displacement-map image


- 1.25.2
  - Bug fix: Fix FileSystemNotFoundException on start-up due to faulty loading of code editor file


- 1.25.1
  - Add drop-down under Audio settings for recording with different audio samples
  - Supports `UINT8`, `INT8`, `INT16`, `INT24`, and `INT32`
  - Thanks very much to [DJLevel3](https://github.com/DJLevel3) for the suggestion and contribution!


- 1.25.0
  - Live-coding and editing/creating/deleting files!
  - Files can now be created, edited, and closed by using the respective buttons in the "Main settings" panel
    - To create a new file, enter a file name, and select the file type from the dropdown before clicking "Create File"
      - Each supported file type has a demo/example file:
        - `.obj` displays the default cube
        - `.svg` renders a circle from an SVG path
        - `.lua` is an example of audio synthesis using Lua that displays an oscillating `tan` function
        - `.txt` renders the word "hello"
    - Closing a file will remove it from the current project
    - Editing a file will open a code editor, allowing you to change the text for all supported file types
      - Any edits will update the image being displayed immediately
      - You can safely close the editor whenever you are finished editing - your edits are saved automatically
    - When you create or edit a file, the title of the window will update to remind you to save the current project in order to save your changes
    - Any changes to files are only made to the osci-render project, and will NOT modify real files on your computer
  - `.lua` file support added
    - Scripts using the [Lua language](https://www.lua.org/) can be opened and live-edited to create your own completely custom audio source!
    - Slider values under ".lua file settings" can be accessed within the script
      - Accessed as variable names: `slider_a`, `slider_b`, `slider_c`, `slider_d`, and `slider_e`
      - Can be used to control parameters in your script, e.g. frequency, volume, or anything else!
    - Variable `step` is passed in and incremented every time the script is called
      - This can be used as a 'phase' or 'theta' input into a sine, cosine, etc. wave
      - e.g. `return { math.sin(step / 1000), math.cos(step / 1000) }` would draw a circle
    - Variables declared in a script are maintained between calls to the script
    - Demo/example `.lua` script can be viewed and edited by creating a new `.lua` file in the "Main settings" panel
      - This gives a practical example of how this can be used to synthesise audio


- 1.24.8
  - Mono and more than 2 audio outputs supported
  - Any audio output after the first L and R channels are used for brightness
    - The brightness is controllable under Audio > Brightness
    - This will only work if the audio interface being used is DC-coupled as it is a static voltage signal


- 1.24.7
  - Double-click of slider resets value to 0 rather than min


- 1.24.6
  - Effect text boxes are now editable
  - Version number now visible in File menu
  - You can now double-click sliders to reset their values to minimum


- 1.24.5
  - Bug fix: MIDI CC signals can now be used to change frequency slider


- 1.24.4
  - Fix various bugs with MIDI synthesis
    - Attack and decay now applied to sine waves in background
    - MIDI synthesis now sounds far cleaner
  - MIDI attack and decay is now configurable from the MIDI menu


- 1.24.3
  - Update description of osci-render application


- 1.24.2
  - Massively improve audio stability on Mac
  - Remove audio stability toggle as no longer needed!


- 1.24.1
  - Various optimisations
  - Mic-based control is now slightly smoother


- 1.24.0
  - Add spinners to all effect sliders
  - Add spinner to frequency slider


- 1.23.0
  - Add software oscilloscope to visualise output!
  - Open from Window > Open Software Oscilloscope


- 1.22.4
  - Add option Audio > High Audio Stability
  - This increases audio latency but significantly improves audio stability - especially on lower-end machines


- 1.22.3
  - Make interface usable whilst loading files


- 1.22.2
  - Revert to CPU execution if an exception occurs when trying to use GPU


- 1.22.1
  - Gracefully handle disconnects from both Blender and osci-render to prevent restarts!


- 1.22.0
  - Add support for externally rendered lines!
  - This means there is now Blender support!
  - Blender add-on included in /blender/osci-render.zip
    - Settings under 'osci-render settings' section in the 'Render Properties' section of Blender
    - Simply connect to osci-render whilst it is open
    - Sends any grease pencil lines over to osci-render
    - This includes any objects/scenes with the line art modifier
  - Report any potential as still in early development!
  - YouTube video explaining how to use this with Blender in development


- 1.21.1
  - Significantly improved .obj rendering performance on larger objects
  - Reduce framerate when drawing too computationally intensive scenes rather than pausing audio (less jarring framerate changes)
  - Correctly skip lines between objects with disconnected sections


- 1.21.0
  - Introduced GPU-based processing of objects
    - THIS IS POTENTIALLY UNSTABLE - PLEASE REPORT ANY ISSUES
  - Added option to remove hidden edges from meshes
    - This can be toggled in View > Hide Hidden Edges
  - Moved device options and recording options to Audio on the menu bar
  - Reshuffled interface to reduce height and increase length of sliders

Versions prior to 1.21.0 are undocumented!
