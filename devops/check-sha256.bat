@echo off

setlocal

rem
rem `set /p` won't work within a single batch file, so we need to call
rem this batch file with the SHA-256 value in the standard input, so
rem we can evaluate it and return success (0) or failure (1).
rem
set /p SHA256_7zip=

rem filtered 7zip output from `7z h -scrcSHA256 sha256.zip | findstr /C:"SHA256 for data"`
set SHA256=%SHA256_7zip:~-64%

if /I "%SHA256%" == "%~1" (
  exit 0
) else (
  exit 1
)
