@echo off

set "VSPATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe -latest -property installationPath"

call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" > nul
"%1" -c "export -p"