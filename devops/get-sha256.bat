@echo off

setlocal

if EXIST src\sha256 goto :haveit

set GIT_COMMIT=5e637272c13f200872d55ff579f7e2ab6c3f252f
set SIG_SHA256=4269c41e5ccbfdb108cc8bba03a95456c36555213cc10965a70e33af0e08292a

curl --location --output sha256.zip https://github.com/jb55/sha256.c/archive/%GIT_COMMIT%.zip

call 7z h -scrcSHA256 sha256.zip | findstr /C:"SHA256 for data" | call devops\check-sha256 "%SIG_SHA256%"

if ERRORLEVEL 1 (
  echo.
  echo Bad SHA-256 signature: sha256.zip
  goto :EOF
)

call 7z x sha256.zip
del /Q sha256.zip

move sha256.c-%GIT_COMMIT% src\sha256
cd src\sha256

move deps\rotate-bits .

rmdir /S /Q deps
del /S /Q package.json
del .gitignore .travis.yml Makefile
del test.c

goto :EOF

:haveit

echo Directory sha256 already exists
