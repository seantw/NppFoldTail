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
rem Recent MinGW-w64 CRTs define __mingw_fe_dfl_env in CRT_fp10.o.
rem DLL links do not add this object automatically, unlike executable links.
for /f "delims=" %%I in ('g++ -print-file-name^=CRT_fp10.o') do set "CRT_FP10=%%I"
if not exist "%CRT_FP10%" (
  echo Missing MinGW-w64 CRT_fp10.o.
  goto :failed
)
windres --target=pe-i386 -I src -i src/plugin.rc -o build/version.o
if errorlevel 1 goto :failed
g++ -m32 -std=c++17 -Os -s -ffunction-sections -fdata-sections -Wall -Wextra -Werror -shared -static -static-libgcc -static-libstdc++ ^
  src/plugin.cpp build/version.o "%CRT_FP10%" -o build/NppFoldTail.dll ^
  -lcomctl32 -luser32 -lgdi32 -lshell32 -Wl,--gc-sections -Wl,--no-insert-timestamp
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
