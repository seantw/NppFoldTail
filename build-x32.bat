@echo off
setlocal
pushd "%~dp0" || exit /b 1

where g++ >nul 2>&1
if errorlevel 1 goto :missing_tools
where windres >nul 2>&1
if errorlevel 1 goto :missing_tools
g++ -dumpmachine | findstr /r /b "i686-.*mingw" >nul
if errorlevel 1 (
  echo Use an i686 MinGW-w64 compiler.
  goto :failed
)

if not exist build mkdir build
if not exist build goto :failed
windres --target=pe-i386 -I src -i src/plugin.rc -o build/version.o
if errorlevel 1 goto :failed
g++ -m32 -std=c++17 -Os -s -ffunction-sections -fdata-sections -Wall -Wextra -Werror -shared -static -static-libgcc -static-libstdc++ ^
  src/plugin.cpp build/version.o -o build/NppFoldTail.dll ^
  -lcomctl32 -luser32 -lgdi32 -Wl,--gc-sections -Wl,--no-insert-timestamp
if errorlevel 1 goto :failed

del /q build\version.o
echo Built build\NppFoldTail.dll
popd
exit /b 0

:missing_tools
echo Missing g++ or windres. Add the i686 MinGW-w64 tools to PATH.
:failed
echo Build failed.
popd
exit /b 1
