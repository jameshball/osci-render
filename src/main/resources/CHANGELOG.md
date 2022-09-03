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
