@echo off
setlocal

set ROOT=%~dp0
set OUT=%ROOT%build\msvc

if not exist "%OUT%" mkdir "%OUT%"

cl /nologo /EHsc /std:c++17 /W4 ^
  /I "%ROOT%third-party\openvr\headers" ^
  /I "%ROOT%third-party\nlhomann" ^
  "%ROOT%src\main.cpp" ^
  /Fe:"%OUT%\vr-piston-animation.exe" ^
  /link "%ROOT%third-party\openvr\lib\win64\openvr_api.lib" ws2_32.lib

if errorlevel 1 exit /b %errorlevel%

copy /y "%ROOT%third-party\openvr\bin\win64\openvr_api.dll" "%OUT%\openvr_api.dll" >nul
if errorlevel 1 exit /b %errorlevel%

echo Built: %OUT%\vr-piston-animation.exe
