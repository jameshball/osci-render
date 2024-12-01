#!/bin/bash -e

PLUGIN="osci-render"

# Test mac version
if [ "$OS" = "mac" ]; then
  curl -L "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_macOS.zip" -o pluginval.zip
  unzip pluginval
  pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --verbose --output-dir "$ROOT/bin" --validate "~/Library/Audio/Plug-Ins/VST3/$PLUGIN.vst3" || exit 1
  pluginval.app/Contents/MacOS/pluginval --strictness-level 10 --verbose --output-dir "$ROOT/bin" --validate "~/Library/Audio/Plug-Ins/Components/$PLUGIN.component" || exit 1
fi

# Test linux version
if [ "$OS" = "linux" ]; then
  cd "$ROOT/bin"

  curl -L "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Linux.zip" -o pluginval.zip
  unzip pluginval
  ./pluginval --strictness-level 10 --verbose --output-dir "$ROOT/bin" --validate "$ROOT/Builds/LinuxMakefile/build/$PLUGIN.vst3" || exit 1
fi

# Build Win version
if [ "$OS" = "win" ]; then
  cd "$ROOT/bin"

  curl -L "https://github.com/Tracktion/pluginval/releases/latest/download/pluginval_Windows.zip" -o pluginval.zip
  unzip pluginval
  pluginval.exe --strictness-level 10 --verbose --output-dir "$ROOT/bin" --validate "$ROOT/Builds/VisualStudio2022/x64/Release/VST3/$PLUGIN.vst3/Contents/x86_64-win/$PLUGIN.vst3" || exit 1
fi

cd "$ROOT"
