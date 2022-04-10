@echo off

if EXIST sha256 goto :haveit

rem
rem This library is not versioned, so get the source at
rem the commit where it was tested with this project.
rem
curl -L -o sha256.zip https://github.com/ilvn/SHA256/archive/28c561fa25b62dc9fce075c1af21ee2579cf1d59.zip

call 7z x sha256.zip
del /Q sha256.zip

rename SHA256-28c561fa25b62dc9fce075c1af21ee2579cf1d59 sha256
cd sha256

rem
rem Patch the SHA256 source to compile in VC++
rem
echo.
"%ProgramFiles%\Git\usr\bin\patch" -i ..\sha256.patch

cd ..

goto :EOF

:haveit

echo Directory sha256 already exists
