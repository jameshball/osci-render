@echo off

set VSWHERE="C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere"
for /f "tokens=*" %%i in ('%VSWHERE% -latest -property installationPath') do set VSWHERE2=%%i

call "%VSWHERE2%\Common7\Tools\VsDevCmd.bat" > nul
bash -c "export -p"