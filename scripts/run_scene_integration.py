#!/usr/bin/env python3
"""Link scene integration checks against an existing macOS Debug app build.

Run after building the standalone app. Pass its xcodebuild log as the argument.
The compiler response file in that log supplies the exact app configuration.
"""
import os
import shlex
import subprocess
import sys
import tempfile
from pathlib import Path

root = Path(__file__).resolve().parents[1]
log = Path(sys.argv[1] if len(sys.argv) > 1 else '/tmp/scene-build.log').read_text()
compile_line = next(line for line in log.splitlines() if ' -c ' in line and '/Source/PluginEditor.cpp ' in line)
args = shlex.split(compile_line)
response = next(arg for arg in args if arg.startswith('@'))
archive_dir = root / 'Builds/osci-render/MacOSX/build/Debug'
with tempfile.TemporaryDirectory(prefix='osci-scene-integration-') as directory:
    temp = Path(directory)
    obj = temp / 'tests.o'
    executable = temp / 'scene-integration'
    subprocess.run(['clang++', '-x', 'c++', response, '-I' + str(root / 'JuceLibraryCode'), '-c', str(root / 'tests/SceneIntegrationHarness.cpp'), '-o', str(obj)], cwd=archive_dir.parent.parent, check=True)
    frameworks = ['IOSurface', 'Accelerate', 'AudioToolbox', 'Cocoa', 'CoreAudio', 'CoreAudioKit', 'CoreMIDI', 'DiscRecording', 'Foundation', 'IOKit', 'OpenGL', 'QuartzCore', 'Security', 'WebKit', 'Metal', 'MetalKit']
    command = ['clang++', str(obj), '-L' + str(archive_dir), '-L' + str(root / 'modules/osci_scripting/third_party/LuaJIT/src'), '-losci-render', '-lluajit', '-Wl,-weak_reference_mismatches,weak']
    for framework in frameworks:
        command += ['-framework', framework]
    subprocess.run(command + ['-o', str(executable)], check=True)
    env = os.environ.copy()
    env['HOME'] = str(temp)
    env['CFFIXED_USER_HOME'] = str(temp)
    subprocess.run([str(executable), str(root)], env=env, check=True, timeout=90)
