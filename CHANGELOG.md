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