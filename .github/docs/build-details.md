# Build Details

Supplementary build documentation. See [copilot-instructions.md](../copilot-instructions.md) for primary guide.

## ccache + PCH (macOS)

Debug builds use two compile-time optimisations wired into the `.jucer` files:

- **PCH** — `Source/pch.h` pre-parses `<JuceHeader.h>` once. Halves clean build time (~2m49s → ~1m12s).
- **ccache** — compiler output cached by preprocessed-source hash. Wired via `ci/ccache-clang` / `ci/ccache-clang++`.

**One-time setup (required on a new machine):**
```bash
brew install ccache
ccache --set-config sloppiness=pch_defines,time_macros,include_file_mtime,include_file_ctime
```

Without the sloppiness config, every build reports "uncacheable" and the cache is never used.

## Building (Windows)

MSBuild must run from a Visual Studio Developer environment so `luajit_win.bat` finds the MSVC toolchain.

```powershell
# 1. Import VS 2022 environment
$vsPath = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
cmd /c "`"$vsPath\VC\Auxiliary\Build\vcvars64.bat`" >nul 2>&1 && set" |
    ForEach-Object { if ($_ -match '^([^=]+)=(.*)$') {
        [System.Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process') } }

# 2. Resave and build
C:\JUCE\Projucer.exe --resave osci-render.jucer
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m
```

### Fast iteration (Windows)

```powershell
Stop-Process -Name "osci-render" -ErrorAction SilentlyContinue
MSBuild Builds\osci-render\VisualStudio2022\osci-render.sln /p:Configuration=Debug /p:Platform=x64 /m
Start-Process "Builds\osci-render\VisualStudio2022\x64\Debug\Standalone Plugin\osci-render.exe"
```
