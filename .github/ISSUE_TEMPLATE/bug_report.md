---
name: Bug report
about: Create a report to help us improve
title: ''
labels: bug
assignees: jameshball

---

**Describe the bug**
A clear and concise description of what the bug is.

**To Reproduce**
Steps to reproduce the behaviour:
1. Go to '...'
2. Click on '....'
3. Scroll down to '....'
4. See error

**Expected behaviour**
A clear and concise description of what you expected to happen.

**Screenshots**
If applicable, add screenshots to help explain your problem.

If your bug is an audio-related issue (specifically, when you open the program, the GUI displays correctly, but there is no audio) then please complete the following additional steps:

1. Attach a screenshot of your connected devices (you can find this on Windows by right-clicking the speaker in your taskbar and clicking Sounds)
2. Detail the default format of your default device
    - You can see this on Windows by right-clicking the device in the Sounds menu and clicking Properties and then Advanced
3. If possible, test your other audio devices (setting them as default and re-opening the program)
    - If another device *does* work, then please detail the format of this device, as you did in step 2

The next steps should be carried out if you are on Windows:

4. If it is still not working, download https://sjoerdvankreel.github.io/xt-audio/dist/xt-audio-1.9.zip and extract it (using 7zip) and run dist\net\gui\Release\net48\Xt.Gui.exe in the extracted folder.
5. Attach three screenshots of this program in full
    - The first screenshot should be of the window with the System dropdown set to ASIO
    - The next screenshot is the same, but with System set to DirectSound
    - Finally, the last screenshot is the same but with System set to WASAPI

**Desktop (please complete the following information):**
 - OS: [e.g. Windows 10]
 - Version [i.e. Build number etc.]

**Additional context**
Add any other context about the problem here.
