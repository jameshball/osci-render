# osci-render

Program for drawing objects, text, and images on an oscilloscope using audio output.

This allows for 3D rendering of `.obj` files, `.svg` images, and `.txt` files.

Lots of this code was built as part of a 24hr hackathon: IC Hack 20. The original repository can be found here: https://github.com/wdhg/ICHack20 It won 'Best Newcomers Prize' at the event.

### Video Demonstration

[![osci-render demonstration](https://img.youtube.com/vi/oEX0mnv6PLM/0.jpg)](https://www.youtube.com/watch?v=oEX0mnv6PLM)

## Current Features

- Render `.obj` files on an oscilloscope
- Render `.svg` files
- Render text
- Rotation of objects
- Scaling image
- Translating image

## Proposed Features

- Tune and transpose audio output
- Support rendering of multiple objects
- Saving pre-rendered frames to file for later loading
- Saving audio output to file
- (long term) Implement GUI
- (long term) Keyframing/animating objects and camera

## Usage

Using osci-render is very easy; specify the file you would like to render, and optionally the rotation speed, and camera focal length and position in the case of `.obj` files and it will output as audio to visualise on your oscilloscope.

To render the cube example in `/models`, you would use the following args:

```
osci-render models/cube.obj 3 1
```

This will render a cube with a rotation speed of 3 and a focal length of 1. Detailed argument usage below:

### Program Arguments

```
osci-render objFile [rotateSpeed] [focalLength] [cameraX] [cameraY] [cameraZ]
OR
osci-render svgFile
OR
osci-render textFile [fontSvgPath]
```

If no camera position arguments are given, the program will automatically move the camera to a position that renders the whole object on the screen with minimal display clipping.

#### Required

- filePath
    - Specifies the file path to retrieve the `.obj`, `.svg`, or `.txt` file

#### Optional

- rotateSpeed
    - Sets the speed of rotation of the object
- focalLength
    - Sets the camera's focal length
- cameraX/Y/Z
    - Sets X/Y/Z position of camera
- fontSvgPath
    - Sets the `.svg` file used as a source for the font used when text rendering

## Building

### Library setup

#### Libraries

- [XT-Audio](https://sjoerdvankreel.github.io/xt-audio/)
- [JGraphT](https://jgrapht.org/)
- [JUnit 4](https://junit.org/junit4/)
- [java-data-front](https://github.com/mokiat/java-data-front)
- [jsoup](https://jsoup.org/)

Open the folder in your IDE of choice (I would recommend using IntelliJ) and build/run `AudioClient` after setting up libraries as provided in `/lib`. You will additionally require a platform-specific `dll`/`so` file for XT-Audio depending if you're on a windows/linux x86/x64 OS, which you'll find in the java-xt folder in the [XT-Audio](https://sjoerdvankreel.github.io/xt-audio/) binaries.

Make sure your default playback devices are setup correctly, as XT-Audio will try and find your default device for audio output.

osci-render is currently using JDK 15, though anything past Java 10 will probably be fine.

## Contact

James Ball, [james@ball.sh](mailto:james@ball.sh)

## Special Thanks

IC Hack 20 Team Members: [James Ball](https://github.com/jameshball), [William Grant](https://github.com/wdhg), [Jessica Lally](https://github.com/jessicalally), [Noor Sawhney](https://github.com/noor-gate), and [Andy Wang](https://github.com/cbeuw)
