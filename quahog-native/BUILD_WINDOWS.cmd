@echo off
setlocal
pushd "%~dp0"
where zig.exe >nul 2>nul
if errorlevel 1 (
  echo Install Zig 0.13.0 and add zig.exe to PATH to rebuild the source.
  echo Players do not need Zig: the compiled DLL is already included.
  popd
  exit /b 1
)
zig cc -target x86_64-windows-gnu -std=c11 -O2 -Wall -Wextra -Werror -shared -Isdk src/quahog_native.c src/qcore.c src/qformats.c src/qhost.c src/qmacho.c src/qmac.c -luser32 -o bin/windows-x86_64/quahog_native.dll
set "QUAHOG_BUILD_RESULT=%ERRORLEVEL%"
popd
exit /b %QUAHOG_BUILD_RESULT%
