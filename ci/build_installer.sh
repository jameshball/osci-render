#!/bin/bash -e

APP="osci-installer"
OUTPUT_NAME="$APP"

RESAVE_COMMAND="$PROJUCER_PATH --resave '$ROOT/$APP.jucer'"
eval "$RESAVE_COMMAND"

if [ "$OS" = "mac" ]; then
  cd "$ROOT/Builds/$APP/MacOSX"
  xcodebuild -project "$APP.xcodeproj" \
    -scheme "$APP - App" \
    -configuration Release \
    ARCHS="arm64 x86_64" \
    ONLY_ACTIVE_ARCH=NO \
    -parallelizeTargets \
    -jobs "$(sysctl -n hw.logicalcpu)"

  INSTALLER_BINARY="$ROOT/Builds/$APP/MacOSX/build/Release/$APP.app/Contents/MacOS/$APP"
  lipo "$INSTALLER_BINARY" -verify_arch arm64
  lipo "$INSTALLER_BINARY" -verify_arch x86_64
fi

if [ "$OS" = "linux" ]; then
  cd "$ROOT/Builds/$APP/LinuxMakefile"
  make -j"$(nproc)" CONFIG=Release

  cp "$ROOT/Builds/$APP/LinuxMakefile/build/$APP" "$ROOT/bin/$OUTPUT_NAME"
  chmod +x "$ROOT/bin/$OUTPUT_NAME"
fi

if [ "$OS" = "win" ]; then
  eval "$($(cygpath "$COMSPEC") /c$(cygpath -w "$ROOT/ci/vcvars_export.bat"))"

  cd "$ROOT/Builds/$APP/VisualStudio2022"
  msbuild.exe "//m" "$APP.sln" \
    "//p:VisualStudioVersion=16.0" \
    "//p:MultiProcessorCompilation=true" \
    "//p:CL_MPCount=16" \
    "//p:BuildInParallel=true" \
    "//t:Build" \
    "//p:Configuration=Release" \
    "//p:Platform=x64" \
    "//p:PreferredToolArchitecture=x64" \
    "//restore" \
    "//p:RestorePackagesConfig=true"

  cp "$ROOT/Builds/$APP/VisualStudio2022/x64/Release/App/$APP.exe" "$ROOT/bin/$OUTPUT_NAME.exe"
fi

cd "$ROOT"
