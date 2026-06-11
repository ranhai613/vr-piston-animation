@echo off
setlocal

set ROOT=%~dp0
set OUT=%ROOT%build\msvc

if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /EHsc /std:c++17 /W4 ^
  /I "%ROOT%..\third-party\openvr\headers" ^
  "%ROOT%src\main.cpp" ^
  /Fe:"%OUT%\vr-offset-animation.exe" ^
  /link "%ROOT%..\third-party\openvr\lib\win64\openvr_api.lib" ws2_32.lib

if errorlevel 1 exit /b %errorlevel%

echo Built: %OUT%\vr-offset-animation.exe
