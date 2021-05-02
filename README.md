<p align="center">
  <img width="267" height="256" src="osci.png" />
</p>

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
- GUI for controlling renderer

## Proposed Features

- Tune and transpose audio output
- Support rendering of multiple objects
- Saving pre-rendered frames to file for later loading
- Saving audio output to file
- (long term) Keyframing/animating objects and camera

## Usage

Using osci-render is very easy; run the program and choose the file you would like to render, and it will output as audio to visualise on your oscilloscope.

By default, the program loads the `cube.obj` example. If this is working, you're good to go and should be able to load your own objects, files, or images!

Control the output using the sliders and text boxes provided. Currently the following can be controlled:

- Translation and speed of translation of the output
- Weight of the lines drawn
- Rotation speed
- Scale of the image

There are some additional controls for `.obj` files:

- Focal length of camera
- Position of camera

## Building

All dependencies are specified in the `pom.xml` file. Cloning this repo and using IntelliJ with Maven should make building a painless process.

## Contact

James Ball, [james@ball.sh](mailto:james@ball.sh)

## Special Thanks

IC Hack 20 Team Members: [James Ball](https://github.com/jameshball), [William Grant](https://github.com/wdhg), [Jessica Lally](https://github.com/jessicalally), [Noor Sawhney](https://github.com/noor-gate), and [Andy Wang](https://github.com/cbeuw)
