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