if /I "%VSTEL_MSBuildProjectFullPath:~-18%" neq "SharedCode.vcxproj" (
  goto skip_luajit
)

cd ..\..\..\modules\LuaJIT\src
.\msvcbuild.bat static

:skip_luajit