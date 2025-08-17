@echo off
if "%VisualStudioVersion%"=="" (
  ECHO Visual Studio command line variables not detected!
  ECHO This script will only work if you run it from a Visual Studio command line!
  goto error_luajit
)

if /I "%VSTEL_MSBuildProjectFullPath:~-18%" neq "SharedCode.vcxproj" (
  goto finish_luajit
)

cd ..\..\..\modules\LuaJIT\src
call .\msvcbuild.bat static
copy /b lua51.lib luajit51.lib
goto finish_luajit

:error_luajit

timeout 10

:finish_luajit
@echo on